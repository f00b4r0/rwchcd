//
//  filecfg/parse/filecfg_parser.c
//  rwchcd
//
//  (C) 2019-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File config parser implementation.
 *
 * The configuration format follows an ISC inspired syntax, with ending semicolons
 * after each declaration; and brackets to nest elements in blocks, also terminated with semicolons.
 *
 * The following rules apply:
 * - All options identifiers match the related C-struct field name and are unquoted.
 * - All integer and decimal values must be specified without quotes.
 * - bool options accept one of the following values (lowercase, without quotes):
 * 	- `true`
 * 	- `on`
 * 	- `yes`
 * 	- `false`
 * 	- `off`
 * 	- `no`
 * - All user strings @b MUST be quoted (single and double quotes accepted) and are case-sensitive.
 * - Comments: to comment the configuration, one can use:
 *	- C++-style `//` single-line comment (all text following will be treated as comment until next line).
 *	- Perl-style `#` single-line comment (all text following will be treated as comment until next line).
 *	- C-style `/ * ... * /` (without space between slash and star) multi-line comments (all text enclosed between opening slash-star and closing star-slash will be ignored, even if it spans multiple lines).
 *
 * Type specific rules:
 * - All `enum` types expect user strings as specified in the corresponding `enum` definition.
 * - All `timekeep_t` values must be expressed in integer seconds or unquoted compound expressions in the form `[0-9]+[wdhms]` with or without whitespace between each time compound, e.g. `2h3m 5s`.
 * - All `temp_t` values must be expressed in Celsius degrees (integer or decimal accepted).
 * - All `valves_`, `pump_` and `bmodel` settings expect a quoted string referencing the name of the related item.
 * - All `schedid_t` settings expect a quoted string referencing the name of the target schedule.
 * - All `inid_t` and `outid_t` settings expect a quoted string referencing the name of the target input or output matching the expected type (temperature, relay).
 *
 * @note several parser do not include a graceful cleanup path as it is assumed that parsing failure will result in the program being aborted.
 * @note the order of the various entities generally does not matter, except for e.g. inputs and outputs where order does matter with respect to the "use first" option.
 *
\verbatim
 backends { ... };
 inputs { ... };
 outputs { ... };
 defconfig { ... };
 models { ... };
 plant { ... };
 storage { ... };
 log { ... };
 scheduler { ... };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lib.h"
#include "filecfg_parser.h"

#include "backends_parse.h"
#include "plant_parse.h"
#include "models_parse.h"
#include "scheduler_parse.h"
#include "storage_parse.h"
#include "log_parse.h"
#include "inputs_parse.h"
#include "outputs_parse.h"

#include "runtime.h"

/**
 * Extract a temperature value from config.
 * This function will handle a temperature value (in Celsius/Kelvin) expressed as
 * either a pure int or a decimal value. It will "ensure" that the parsed value is within RWCHCD_TEMPMIN-RWCHCD_TEMPAX range.
 * @param positiveonly true if strictly negative values should be rejected
 * @param delta true if the extracted temperature should be considered a delta (in Kelvin), false if absolute temp (in Celsius)
 * @param n the configuration node to parse
 * @param priv a pointer to temp_t storage for the extracted value
 * @return exec status
 */
int filecfg_parser_get_node_temp(bool positiveonly, bool delta, const struct s_filecfg_parser_node * const n, void *priv)
{
	temp_t *temp = priv;
	temp_t t;
	float fv; int iv;

	assert((NODEFLT|NODEINT) & n->type);

	if (n->children)
		return(-ENOTWANTED);

	if (NODEFLT == n->type) {
		fv = n->value.floatval;
		if (positiveonly && (fv < 0))
			return (-EINVALID);
		t = delta ? deltaK_to_temp(fv) : celsius_to_temp(fv);
	} else { /* NODEINT */
		iv = n->value.intval;
		if (positiveonly && (iv < 0))
			return (-EINVALID);
		t = delta ? deltaK_to_temp(iv) : celsius_to_temp(iv);
	}

	// minimum sanity checks
	if (delta) {
		if (abs((tempdiff_t)t) >= (RWCHCD_TEMPMAX - RWCHCD_TEMPMIN)) {
			filecfg_parser_pr_err(_("Configured temperature delta out of range at line %d"), n->lineno);
			return (-EINVALID);
		}
	} else {
		if ((RWCHCD_TEMPMIN > t) || (RWCHCD_TEMPMAX < t)) {
			filecfg_parser_pr_err(_("Configured temperature out of range at line %d"), n->lineno);
			return (-EINVALID);
		}
	}

	*temp = t;
	return (ALL_OK);
}

/**
 * Create a new configuration node.
 * This routine is used by the Bison parser.
 * @param lineno the line number for the new node
 * @param type the type of the new node
 * @param name the name of the new node
 * @param value the value of the new node
 * @param children the children of the new node (if any)
 * @return a properly populated node structure
 * @note the function will forcefully exit if OOM
 */
struct s_filecfg_parser_node * filecfg_parser_new_node(int lineno, int type, char *name, union u_filecfg_parser_nodeval value, struct s_filecfg_parser_nodelist *children)
{
	struct s_filecfg_parser_node * node = calloc(1, sizeof(*node));

	if (!node) {
		perror(NULL);
		exit(-1);
	}
#if 0
	printf("new_node: %d, %d, %s, ", lineno, type, name);
	switch (type) {
		case NODEINT:
		case NODEBOL:
			printf("%d\n", value.intval);
			break;
		case NODEFLT:
			printf("%f\n", value.floatval);
			break;
		case NODESTR:
			printf("%s\n", value.stringval);
			break;
		case NODELST:
			printf("{list}\n");
			break;
	}
#endif
	node->lineno = lineno;
	node->type = type;
	node->name = name;
	node->value = value;
	node->children = children;

	return (node);
}

/**
 * Insert a configuration node into a node list.
 * This routine is used by the Bison parser.
 * @param next a pointer to the next list member
 * @param node a pointer to the node to insert
 * @return the newly created list member
 * @note the function will forcefully exit if OOM
 */
struct s_filecfg_parser_nodelist * filecfg_parser_new_nodelistelmt(struct s_filecfg_parser_nodelist *next, struct s_filecfg_parser_node *node)
{
	struct s_filecfg_parser_nodelist * listelmt = calloc(1, sizeof(*listelmt));

	if (!listelmt) {
		perror(NULL);
		exit(-1);
	}

	if (next)
		next->prev = listelmt;
	listelmt->next = next;
	listelmt->node = node;

	return (listelmt);
}

int filecfg_parser_sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	const struct {
		const char * pstr;
		const enum e_systemmode psm;
	} params[] = {
		{ "off",	SYS_OFF,	},
		{ "auto",	SYS_AUTO,	},
		{ "comfort", 	SYS_COMFORT,	},
		{ "eco",	SYS_ECO,	},
		{ "frostfree",	SYS_FROSTFREE,	},
		{ "test",	SYS_TEST,	},
		{ "dhwonly",	SYS_DHWONLY,	},
		{ "manual",	SYS_MANUAL,	},
	};
	enum e_systemmode * restrict const sysmode = priv;
	enum e_systemmode sm = SYS_UNKNOWN;
	const char * restrict n;
	unsigned int i;

	if (node->children)
		return(-ENOTWANTED);

	n = node->value.stringval;

	for (i = 0; i < ARRAY_SIZE(params); i++) {
		if (!strcmp(n, params[i].pstr)) {
			sm = params[i].psm;
			break;
		}
	}

	*sysmode = sm;

	if (SYS_UNKNOWN == sm) {
		filecfg_parser_pr_err(_("Unknown systemmode \"%s\" at line %d"), n, node->lineno);
		return (-EINVALID);
	}

	return (ALL_OK);
}

FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_runtime, startup_runmode)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_runtime, startup_dhwmode)

static int runtime_sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_runtime * restrict const runtime = priv;
	return (filecfg_parser_sysmode_parse(&runtime->set.startup_sysmode, node));
}

static int runtime_notifier_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_runtime * restrict const runtime = priv;
	runtime->set.notifier = strdup(node->value.stringval);
	return (ALL_OK);
}

static int runtime_config_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,		"startup_sysmode",	true,	runtime_sysmode_parse,			NULL, },	// 0
		{ NODESTR,		"startup_runmode",	false,	fcp_runmode_s_runtime_startup_runmode,	NULL, },
		{ NODESTR,		"startup_dhwmode",	false,	fcp_runmode_s_runtime_startup_dhwmode,	NULL, },	// 2
		{ NODESTR,		"notifier",		false,	runtime_notifier_parse,			NULL, },
	};
	struct s_runtime * const runtime = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(runtime, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// consistency checks post matching

	if (SYS_MANUAL == runtime->set.startup_sysmode) {
		if (!parsers[1].node || !parsers[2].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: startup_sysmode set to \"manual\" but startup_runmode and/or startup_dhwmode are not set"), node->name, node->lineno);
			return (-EINVALID);
		}
	}

	runtime->set.configured = true;

	return (ALL_OK);
}

/**
 * Parse a list of sibling nodes.
 * @param priv opaque private data pointer
 * @param nodelist the list of sibling nodes
 * @param nname the expected name for sibling nodes
 * @param ntype the expected type for sibling nodes
 * @param parser the parser to apply to each sibling node
 * @return exec status
 * @note this function will parse siblings in the same order they appear in the config file.
 */
int filecfg_parser_parse_siblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist,
				  const char * nname, const enum e_filecfg_nodetype ntype, const parser_t parser)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	const char * sname;
	int ret = -EEMPTY;	// immediate return if nodelist is empty

	assert(parser);

	// by construction the bison parser creates a reverse-ordered list (wrt config file natural order): reverse it
	for (nlist = nodelist; nlist && nlist->next; nlist = nlist->next);

	for (; nlist; nlist = nlist->prev) {
		node = nlist->node;
		if (!(ntype & node->type)) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring node \"%s\" with invalid type closing at line %d\n"), node->name, node->lineno);
			continue;
		}
		if (strcmp(nname, node->name)) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring unknown node \"%s\" closing at line %d\n"), node->name, node->lineno);
			continue;
		}

		if ((NODESTR|NODESTC) & ntype) {
			sname = node->value.stringval;

			if (strlen(sname) < 1) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring \"%s\" with empty name closing at line %d\n"), node->name, node->lineno);
				continue;
			}

			dbgmsg(3, 1, "Trying %s node \"%s\"", node->name, sname);
		}
		else {
			dbgmsg(3, 1, "Trying %s node", node->name);
		}
		// test parser
		ret = parser(priv, node);
		dbgmsg(3, (ALL_OK == ret), "found!");
		if (ALL_OK != ret)
			break;	// stop processing at first fault
	}

	return (ret);
}

/**
 * Count a list of sibling nodes.
 * @param nodelist the list of sibling nodes
 * @param nname the expected name for sibling nodes
 * @return number of siblings found
 */
unsigned int filecfg_parser_count_siblings(const struct s_filecfg_parser_nodelist * const nodelist, const char * nname)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	unsigned int i = 0;

	for (nlist = nodelist; nlist; nlist = nlist->next) {
		node = nlist->node;
		if (strcmp(nname, node->name))
			continue;

		i++;
	}

	return (i);
}

/**
 * Parse a runmode configuration reference.
 * @param priv a pointer to a e_runmode variable which will be populated
 * @param node the configuration node to populate from
 * @return exec status
 */
int filecfg_parser_runmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	const struct {
		const char * pstr;
		const enum e_runmode prm;
	} params[] = {
		{ "off",	RM_OFF,		},
		{ "auto",	RM_AUTO,	},
		{ "comfort", 	RM_COMFORT,	},
		{ "eco",	RM_ECO,		},
		{ "frostfree",	RM_FROSTFREE,	},
		{ "test",	RM_TEST,	},
		{ "dhwonly",	RM_DHWONLY,	},
	};
	enum e_runmode * restrict const runmode = priv;
	enum e_runmode rm = RM_UNKNOWN;
	const char * restrict n;
	unsigned int i;

	assert(NODESTR == node->type);

	if (node->children)
		return(-ENOTWANTED);

	n = node->value.stringval;

	for (i = 0; i < ARRAY_SIZE(params); i++) {
		if (!strcmp(n, params[i].pstr)) {
			rm = params[i].prm;
			break;
		}
	}

	*runmode = rm;

	if (RM_UNKNOWN == rm) {
		filecfg_parser_pr_err(_("Unknown runmode \"%s\" at line %d"), n, node->lineno);
		return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Placeholder that allows matching nodes but returns an error when invoked.
 * This is primarily meant to avoid config warning when hw_backends are tested to match nodes.
 * @param priv unused private data
 * @return -ENOTIMPLEMENTED
 */
int filecfg_parser_unimplemented_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	filecfg_parser_pr_err("\"%s\" parser not implemented!", node->name);
	return (-ENOTIMPLEMENTED);
}


/**
 * Match an indidual node against a list of parsers.
 * @param node the target node to match from
 * @param parsers the parsers to match the node with
 * @param nparsers the number of parsers available in parsers[]
 * @return exec status
 */
int filecfg_parser_match_node(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	bool matched = false;
	unsigned int i;

	if (!node || !parsers || !nparsers)
		return (-EINVALID);

	for (i = 0; i < nparsers; i++) {
		if (!strcmp(parsers[i].identifier, node->name)) {
			if (!(parsers[i].type & node->type)) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring node \"%s\" with invalid type closing at line %d\n"), node->name, node->lineno);
				return (-EINVALID);
			}

			dbgmsg(3, 1, "matched %s, %d", node->name, node->lineno);
			matched = true;
			if (parsers[i].node) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring duplicate node \"%s\" closing at line %d\n"), node->name, node->lineno);
				continue;
			}
			parsers[i].node = node;
		}
	}
	if (!matched) {
		fprintf(stderr, "CONFIG WARNING! Ignoring unknown node \"%s\" closing at line %d\n", node->name, node->lineno);
		return (-EUNKNOWN);
	}

	return (ALL_OK);
}

/**
 * Match a set of parsers with a nodelist members.
 * @param nodelist the target nodelist to match from
 * @param parsers the parsers to match with
 * @param nparsers the number of available parsers in parsers[]
 * @return -ENOTFOUND if a required parser didn't match, ALL_OK otherwise
 */
static int filecfg_parser_match_nodelist(const struct s_filecfg_parser_nodelist * const nodelist, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	const struct s_filecfg_parser_nodelist *list;
	unsigned int i;
	int ret = ALL_OK;

	// cleanup the parsers nodes before run
	for (i = 0; i < nparsers; i++)
		parsers[i].node = NULL;

	// attempt matching
	for (list = nodelist; list; list = list->next)
		filecfg_parser_match_node(list->node, parsers, nparsers);	// ignore return value to report as many errors as possible at once

	// report missing required nodes
	for (i = 0; i < nparsers; i++) {
		if (parsers[i].required && !parsers[i].node) {
			filecfg_parser_pr_err(_("Missing required configuration node \"%s\""), parsers[i].identifier);
			ret = -ENOTFOUND;
		}
	}

	return (ret);
}

/**
 * Match a set of parsers with a node's children members.
 * @param node the target node to match from children
 * @param parsers the parsers to match with
 * @param nparsers the number of available parsers in parsers[]
 * @return -ENOTFOUND if a required parser didn't match, -EEMPTY if node has no children, ALL_OK otherwise
 * @note will report error
 */
int filecfg_parser_match_nodechildren(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	int ret;

	if (!node->children)
		return (-EEMPTY);

	ret = filecfg_parser_match_nodelist(node->children, parsers, nparsers);
	if (ALL_OK != ret)
		filecfg_parser_pr_err(_("Incomplete \"%s\" node configuration closing at line %d"), node->name, node->lineno);

	return (ret);
}

#define filecfg_parser_report_invaliddata(NODE)		filecfg_parser_pr_err("Invalid data for node \"%s\" closing at line %d", NODE->name, NODE->lineno)

/**
 * Trigger all parsers from a parser list.
 * @param priv optional private data
 * @param parsers the parsers to trigger, with their respective .node elements correctly set
 * @param nparsers the number of parsers available in parsers[]
 * @return exec status. @note will abort execution at first error
 * @note reports invalid data
 */
int filecfg_parser_run_parsers(void * restrict const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	unsigned int i;
	int ret;

	for (i = 0; i < nparsers; i++) {
		if (parsers[i].node && parsers[i].parser) {
			dbgmsg(3, 1, "running parser \"%s\"", parsers[i].identifier);
			ret = parsers[i].parser(priv, parsers[i].node);
			if (ALL_OK != ret) {
				filecfg_parser_report_invaliddata(parsers[i].node);
				return (ret);
			}
		}
	}

	return (ALL_OK);
}

/**
 * Process the root list of config nodes.
 * This routine is used by the Bison parser.
 * @param nodelist the root nodelist for all the configuration nodes
 * @return 0 on success, 1 on failure (bison requirement)
 */
int filecfg_parser_process_config(const struct s_filecfg_parser_nodelist * const nodelist)
{
	struct s_filecfg_parser_parsers root_parsers[] = {	// order matters we want to parse backends first and plant last
		{ NODELST,	"storage",	false,	filecfg_storage_parse,	NULL, },	// storage is needed first
		{ NODELST,	"log",		false,	filecfg_log_parse,	NULL, },	// log depends on storage and several entities need it
		{ NODELST,	"backends",	false,	filecfg_backends_parse, NULL, },	// backends are needed by hardware and several backend implementations depend on storage/log
		{ NODELST,	"inputs",	false,	filecfg_inputs_parse,	NULL, },	// inputs depend on hardware
		{ NODELST,	"outputs",	false,	filecfg_outputs_parse,	NULL, },	// outputs depend on hardware
		{ NODELST,	"scheduler",	false,	filecfg_scheduler_parse, NULL, },	// we need schedulers during plant setup
		{ NODELST,	"defconfig",	false,	runtime_config_parse,	NULL, },	// defconfig depends on nothing
		{ NODELST,	"models",	false,	filecfg_models_parse,	NULL, },	// models depend on inputs, storage and log
		{ NODELST,	"plant",	true,	filecfg_plant_parse,	NULL, },	// plant depends on everything else
	};
	struct s_runtime * const runtime = runtime_get();
	int ret;

	pr_log(_("Begin parsing config"));

	if (!nodelist) {
		pr_err("Empty configuration file!");
		return (1);
	}

	ret = filecfg_parser_match_nodelist(nodelist, root_parsers, ARRAY_SIZE(root_parsers));
	if (ALL_OK != ret)
		goto fail;

	ret = filecfg_parser_run_parsers(runtime, root_parsers, ARRAY_SIZE(root_parsers));
	if (ALL_OK != ret)
		goto fail;

	pr_log(_("Config successfully parsed"));
	return (0);

fail:
	switch (ret) {
		case -ENOTWANTED:
			pr_err(_("Unknown extra data in config!"));
			break;
		case -EOOM:
			pr_err(_("Out of memory while parsing configuration!"));
			break;
		default:
			pr_err(_("Error parsing config! (%d)"), ret);
			break;
	}

	return (1);
}

/**
 * Free all elements of a nodelist.
 * This routine is used by the Bison parser.
 * @param nodelist the target nodelist to purge
 */
void filecfg_parser_free_nodelist(struct s_filecfg_parser_nodelist *nodelist)
{
	struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_nodelist *next, *children;

	if (!nodelist)
		return;

	node = nodelist->node;
	next = nodelist->next;
	children = node->children;

	free(nodelist);

	if (node) {
		free(node->name);
		if ((NODESTR|NODESTC) & node->type)
			free(node->value.stringval);
		free(node);
		filecfg_parser_free_nodelist(children);
	}

	filecfg_parser_free_nodelist(next);
}
