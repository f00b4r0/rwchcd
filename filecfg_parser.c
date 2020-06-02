//
//  filecfg_parser.c
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
 *	- `false`
 * 	- `off`
 *	- `no`
 * - All user strings @b MUST be quoted (single and double quotes accepted) and are case-sensitive.
 * - Comments: to comment the configuration, one can use:
 *	- C++-style `//` single-line comment (all text following will be treated as comment until next line).
 * 	- Perl-style `#` single-line comment (all text following will be treated as comment until next line).
 *	- C-style `/ * ... * /` (without space between slash and star) multi-line comments (all text enclosed between opening slash-star and closing star-slash will be ignored, even if it spans multiple lines).
 *
 * Type specific rules:
 * - All `enum` types expect user strings as specified in the corresponding `enum` definition.
 * - All `timekeep_t` values must be expressed in integer seconds or unquoted compound expressions in the form `[0-9]+[wdhms]` with or without whitespace between each time compound, e.g. `2h3m 5s`.
 * - All `temp_t` values must be expressed in Celsius degrees (integer or decimal accepted).
 * - All `valves_`, `pump_` and `bmodel` settings expect a quoted string referencing the name of the related item.
 * - All `schedid_t` settings expect a quoted string referencing the name of the target schedule.
 * - All `rid_` and `tid_` are specified as a block specifying the backend name and the name of the relay or sensor within that backend. For instance:
\verbatim
 rid_open {
 	backend "prototype";
 	name "v_open";
 };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>

#include "hw_backends.h"
#include "config.h"
#include "lib.h"
#include "timekeep.h"
#include "filecfg_parser.h"

#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif

#include "models.h"

#include "plant.h"
#include "filecfg/pump_parse.h"
#include "filecfg/valve_parse.h"
#include "filecfg/dhwt_parse.h"
#include "filecfg/hcircuit_parse.h"
#include "filecfg/heatsource_parse.h"

#include "scheduler.h"

#include "filecfg/models_parse.h"
#include "filecfg/scheduler_parse.h"
#include "filecfg/storage_parse.h"
#include "filecfg/log_parse.h"

#include "runtime.h"

int filecfg_parser_get_node_temp(bool positiveonly, bool delta, const struct s_filecfg_parser_node * const n, void *priv)
{
	temp_t *temp = priv;
	
	float fv; int iv;
	if (NODEFLT == n->type) {
		fv = n->value.floatval;
		if (positiveonly && (fv < 0))
			return (-EINVALID);
		*temp = delta ? deltaK_to_temp(fv) : celsius_to_temp(fv);
	} else { /* NODEINT */
		iv = n->value.intval;
		if (positiveonly && (iv < 0))
			return (-EINVALID);
		*temp = delta ? deltaK_to_temp(iv) : celsius_to_temp(iv);
	}
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

	listelmt->next = next;
	listelmt->node = node;

	return (listelmt);
}

static int hardware_backend_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	int ret = ALL_OK;

#ifdef HAS_HWP1		// XXX
	ret = hw_p1_filecfg_parse(node);
#endif

	return (ret);
}

struct s_fcp_hwbkend {
	struct {
		const char *backend;
		const char *name;
	} set;
};

FILECFG_PARSER_STR_PARSE_SET_FUNC(true, s_fcp_hwbkend, backend)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, s_fcp_hwbkend, name)

int filecfg_parser_tid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	tempid_t * restrict const tempid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"backend",	true,	fcp_str_s_fcp_hwbkend_backend,	NULL, },
		{ NODESTR,	"name",		true,	fcp_str_s_fcp_hwbkend_name,	NULL, },
	};
	struct s_fcp_hwbkend p;
	int ret;

	dbgmsg(3, 1, "Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg(3, 1, "empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(&p, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = hw_backends_sensor_fbn(tempid, p.set.backend, p.set.name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or sensor \"%s\" not found"), node->name, node->lineno, p.set.backend, p.set.name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_sensor_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

int filecfg_parser_rid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	relid_t * restrict const relid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"backend",	true,	fcp_str_s_fcp_hwbkend_backend,	NULL, },
		{ NODESTR,	"name",		true,	fcp_str_s_fcp_hwbkend_name,	NULL, },
	};
	struct s_fcp_hwbkend p;
	int ret;

	dbgmsg(3, 1, "Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg(3, 1, "empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(&p, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = hw_backends_relay_fbn(relid, p.set.backend, p.set.name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or relay \"%s\" not found"), node->name, node->lineno, p.set.backend, p.set.name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_relay_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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

FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_config, limit_tsummer)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_config, limit_tfrost)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_config, startup_runmode)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_config, startup_dhwmode)

static int defconfig_sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	return (sysmode_parse(&config->startup_sysmode, node));
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"limit_tsummer",	false,	fcp_temp_s_config_limit_tsummer,	NULL, },	// 0
		{ NODEFLT|NODEINT,	"limit_tfrost",		false,	fcp_temp_s_config_limit_tfrost,		NULL, },
		{ NODESTR,		"startup_sysmode",	true,	defconfig_sysmode_parse,		NULL, },	// 2
		{ NODESTR,		"startup_runmode",	false,	fcp_runmode_s_config_startup_runmode,	NULL, },
		{ NODESTR,		"startup_dhwmode",	false,	fcp_runmode_s_config_startup_dhwmode,	NULL, },	// 4
	};
	struct s_runtime * const runtime = priv;
	struct s_config * restrict config;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	config = config_new();
	if (!config)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(config, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// consistency checks post matching

	if (SYS_MANUAL == config->startup_sysmode) {
		if (!parsers[3].node || !parsers[4].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: startup_sysmode set to \"manual\" but startup_runmode and/or startup_dhwmode are not set"), node->name, node->lineno);
			return (-EINVALID);
		}
	}

	config->configured = true;
	runtime->config = config;

	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
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
 */
int filecfg_parser_parse_siblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist,
				  const char * nname, const enum e_filecfg_nodetype ntype, const parser_t parser)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	const char * sname;
	int ret = -EEMPTY;	// immediate return if nodelist is empty

	for (nlist = nodelist; nlist; nlist = nlist->next) {
		node = nlist->node;
		if (ntype != node->type) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring node \"%s\" with invalid type closing at line %d\n"), node->name, node->lineno);
			continue;
		}
		if (strcmp(nname, node->name)) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring unknown node \"%s\" closing at line %d\n"), node->name, node->lineno);
			continue;
		}

		if (NODESTR == ntype) {
			sname = node->value.stringval;

			if (strlen(sname) < 1) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring \"%s\" with empty name closing at line %d\n"), node->name, node->lineno);
				continue;
			}

			dbgmsg(3, 1, "Trying %s node \"%s\"", node->name, sname);
		}
		else
			dbgmsg(3, 1, "Trying %s node", node->name);

		// test parser
		ret = parser(priv, node);
		dbgmsg(3, (ALL_OK == ret), "found!");
		if (ALL_OK != ret)
			break;	// stop processing at first fault
	}

	return (ret);
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_plant, summer_maintenance)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, sleeping_delay)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, summer_run_interval)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, summer_run_duration)

static int defconfig_def_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	return (filecfg_hcircuit_params_parse(&plant->set.def_hcircuit, node));
}

static int defconfig_def_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	return (filecfg_dhwt_params_parse(&plant->set.def_dhwt, node));
}

static int plant_config_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"summer_maintenance",	false,	fcp_bool_s_plant_summer_maintenance,	NULL, },
		{ NODEINT|NODEDUR,	"sleeping_delay",	false,	fcp_tk_s_plant_sleeping_delay,		NULL, },
		{ NODEINT|NODEDUR,	"summer_run_interval",	false,	fcp_tk_s_plant_summer_run_interval,	NULL, },
		{ NODEINT|NODEDUR,	"summer_run_duration",	false,	fcp_tk_s_plant_summer_run_duration,	NULL, },
		{ NODELST,		"def_hcircuit",		false,	defconfig_def_hcircuit_parse,		NULL, },
		{ NODELST,		"def_dhwt",		false,	defconfig_def_dhwt_parse,		NULL, },
	};
	struct s_plant * const plant = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(plant, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// consistency checks post matching

	if (plant->set.summer_maintenance) {
		if (plant->set.summer_run_interval || plant->set.summer_run_duration) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: summer_maintenance is set but summer_run_interval and/or summer_run_duration are not set"), node->name, node->lineno);
			return (-EINVALID);

		}
	}

	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
	return (ALL_OK);
}

static int pumps_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "pump", filecfg_pump_parse));
}

static int valves_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "valve", filecfg_valve_parse));
}

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


static int dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "dhwt", filecfg_dhwt_parse));
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", hcircuit_parse));
}


static int heatsources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "heatsource", filecfg_heatsource_parse));
}

static int plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"config",	false,	plant_config_parse,	NULL, },
		{ NODELST,	"pumps",	false,	pumps_parse,		NULL, },
		{ NODELST,	"valves",	false,	valves_parse,		NULL, },
		{ NODELST,	"dhwts",	false,	dhwts_parse,		NULL, },
		{ NODELST,	"hcircuits",	false,	hcircuits_parse,	NULL, },
		{ NODELST,	"heatsources",	false,	heatsources_parse,	NULL, },
	};
	struct s_runtime * const runtime = priv;
	struct s_plant * plant;
	int ret;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// create a new plant
	plant = plant_new();
	if (!plant)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(plant, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	plant->set.configured = true;
	runtime->plant = plant;

	return (ret);
}

static int hardware_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
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
		// dbgmsg as there can be legit mismatch e.g. when parsing foreign backend config
		dbgmsg(3, 1, "Ignoring unknown node \"%s\" closing at line %d", node->name, node->lineno);
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
int filecfg_parser_match_nodelist(const struct s_filecfg_parser_nodelist * const nodelist, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
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
 * @return -ENOTFOUND if a required parser didn't match, ALL_OK otherwise
 * @note will report error
 */
int filecfg_parser_match_nodechildren(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	int ret;

	if (!node->children)
		return (-EINVALID);

	ret = filecfg_parser_match_nodelist(node->children, parsers, nparsers);
	if (ALL_OK != ret)
		filecfg_parser_pr_err(_("Incomplete \"%s\" node configuration closing at line %d"), node->name, node->lineno);

	return (ret);
}

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
 * @return 0 on success, 1 on failure
 */
int filecfg_parser_process_config(const struct s_filecfg_parser_nodelist * const nodelist)
{
	struct s_filecfg_parser_parsers root_parsers[] = {	// order matters we want to parse backends first and plant last
		{ NODELST,	"backends",	false,	hardware_backends_parse, NULL, },
		{ NODELST,	"scheduler",	false,	filecfg_scheduler_parse, NULL, },	// we need schedulers during plant setup
		{ NODELST,	"defconfig",	false,	defconfig_parse,	NULL, },
		{ NODELST,	"models",	false,	filecfg_models_parse,	NULL, },
		{ NODELST,	"plant",	true,	plant_parse,		NULL, },
		{ NODELST,	"storage",	false,	filecfg_storage_parse,	NULL, },
		{ NODELST,	"logging",	false,	filecfg_log_parse,	NULL, },
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
	struct s_filecfg_parser_nodelist *next;

	if (!nodelist)
		return;

	node = nodelist->node;
	next = nodelist->next;

	free(nodelist);

	if (node) {
		free(node->name);
		if (NODESTR == node->type)
			free(node->value.stringval);
		filecfg_parser_free_nodelist(node->children);
	}

	filecfg_parser_free_nodelist(next);
}
