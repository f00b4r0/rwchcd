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
#include "pump.h"
#include "valve.h"
#include "dhwt.h"
#include "hcircuit.h"
#include "heatsource.h"

#include "scheduler.h"
#include "scheduler_filecfg.h"

#include "models_filecfg.h"
#include "storage_filecfg.h"

#include "runtime.h"

#ifndef ARRAY_SIZE
 #define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))
#endif

#define FILECFG_PARSER_INT_PARSE_FUNC(_positiveonly, _struct, _setmember)			\
static int fcp_int_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if (_positiveonly && (iv < 0))						\
		return (-EINVALID);						\
	s->set._setmember = iv;							\
	return (ALL_OK);							\
}

static int __fcp_get_node_celsius_temp(bool positiveonly, bool delta, const struct s_filecfg_parser_node * const n, temp_t *temp)
{
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

#define FILECFG_PARSER_CELSIUS_PARSE_FUNC(_positiveonly, _delta, _struct, _setmember)	\
static int fcp_temp_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int ret; temp_t temp = 0;						\
	ret = __fcp_get_node_celsius_temp(_positiveonly, _delta, n, &temp);	\
	s->set._setmember = temp;	/* Note: always set */			\
	return (ret);								\
}

#define FILECFG_PARSER_RUNMODE_PARSE_FUNC(_struct, _setmember)			\
static int fcp_runmode_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_parser_runmode_parse(&s->set._setmember, n));		\
}

#define FILECFG_PARSER_PRIO_PARSE_FUNC(_struct, _setmember)			\
static int fcp_prio_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if ((iv < 0) || (iv > UINT_FAST8_MAX))					\
		return (-EINVALID);						\
	s->set._setmember = (typeof(s->set._setmember))iv;			\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_SCHEDID_PARSE_FUNC(_struct, _setmember)			\
static int fcp_schedid_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv; int iv;			\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	iv = scheduler_schedid_by_name(n->value.stringval);			\
	if (iv <= 0)								\
		return (-EINVALID);						\
	s->set._setmember = (unsigned)iv;					\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_PBMODEL_PARSE_FUNC(_struct, _setpmember)			\
static int fcp_bmodel_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = models_fbn_bmodel(n->value.stringval);		\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
}

int log_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

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

int filecfg_parser_tid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	tempid_t * restrict const tempid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "backend", true, NULL, NULL, },
		{ NODESTR, "name", true, NULL, NULL, },
	};
	const char * backend, * name;
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

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	ret = hw_backends_sensor_fbn(tempid, backend, name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or sensor \"%s\" not found"), node->name, node->lineno, backend, name);
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
		{ NODESTR, "backend", true, NULL, NULL, },
		{ NODESTR, "name", true, NULL, NULL, },
	};
	const char * backend, * name;
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

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	ret = hw_backends_relay_fbn(relid, backend, name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or relay \"%s\" not found"), node->name, node->lineno, backend, name);
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

static int dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "t_comfort", false, NULL, NULL, },		// 0
		{ NODEFLT|NODEINT, "t_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "t_frostfree", false, NULL, NULL, },		// 2
		{ NODEFLT|NODEINT, "t_legionella", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_tmin", false, NULL, NULL, },		// 4
		{ NODEFLT|NODEINT, "limit_tmax", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_wintmax", false, NULL, NULL, },	// 6
		{ NODEFLT|NODEINT, "hysteresis", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "temp_inoffset", false, NULL, NULL, },	// 8
		{ NODEINT|NODEDUR, "limit_chargetime", false, NULL, NULL, },
	};
	struct s_dhwt_params * restrict const dhwt_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	int iv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		if (NODEFLT == currnode->type) {
			fv = currnode->value.floatval;
			delta = deltaK_to_temp(fv);
			celsius = celsius_to_temp(fv);
		}
		else {	// NODEINT
			iv = currnode->value.intval;
			delta = deltaK_to_temp(iv);
			celsius = celsius_to_temp(iv);
		}

		switch (i) {
			case 0:
				dhwt_params->t_comfort = celsius;
				break;
			case 1:
				dhwt_params->t_eco = celsius;
				break;
			case 2:
				dhwt_params->t_frostfree = celsius;
				break;
			case 3:
				dhwt_params->t_legionella = celsius;
				break;
			case 4:
				dhwt_params->limit_tmin = celsius;
				break;
			case 5:
				dhwt_params->limit_tmax = celsius;
				break;
			case 6:
				dhwt_params->limit_wintmax = celsius;
				break;
			case 7:
				if (delta < 0)
					goto invaliddata;
				else
					dhwt_params->hysteresis = delta;
				break;
			case 8:
				dhwt_params->temp_inoffset = delta;
				break;
			case 9:
				if (currnode->value.intval < 0)
					goto invaliddata;
				else
					dhwt_params->limit_chargetime = timekeep_sec_to_tk(currnode->value.intval);
				break;
			default:
				break;	// never happen
		}
	}

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "t_comfort", false, NULL, NULL, },		// 0
		{ NODEFLT|NODEINT, "t_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "t_frostfree", false, NULL, NULL, },		// 2
		{ NODEFLT|NODEINT, "t_offset", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "outhoff_comfort", false, NULL, NULL, },	// 4
		{ NODEFLT|NODEINT, "outhoff_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "outhoff_frostfree", false, NULL, NULL, },	// 6
		{ NODEFLT|NODEINT, "outhoff_hysteresis", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_wtmin", false, NULL, NULL, },		// 8
		{ NODEFLT|NODEINT, "limit_wtmax", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "temp_inoffset", false, NULL, NULL, },	// 10
	};
	struct s_hcircuit_params * restrict const hcircuit_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	int iv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		if (NODEFLT == currnode->type) {
			fv = currnode->value.floatval;
			delta = deltaK_to_temp(fv);
			celsius = celsius_to_temp(fv);
		}
		else {	// NODEINT
			iv = currnode->value.intval;
			delta = deltaK_to_temp(iv);
			celsius = celsius_to_temp(iv);
		}

		switch (i) {
			case 0:
				hcircuit_params->t_comfort = celsius;
				break;
			case 1:
				hcircuit_params->t_eco = celsius;
				break;
			case 2:
				hcircuit_params->t_frostfree = celsius;
				break;
			case 3:
				hcircuit_params->t_offset = delta;
				break;
			case 4:
				hcircuit_params->outhoff_comfort = celsius;
				break;
			case 5:
				hcircuit_params->outhoff_eco = celsius;
				break;
			case 6:
				hcircuit_params->outhoff_frostfree = celsius;
				break;
			case 7:
				if (delta < 0)
					goto invaliddata;
				else
					hcircuit_params->outhoff_hysteresis = delta;
				break;
			case 8:
				hcircuit_params->limit_wtmin = celsius;
				break;
			case 9:
				hcircuit_params->limit_wtmax = celsius;
				break;
			case 10:
				hcircuit_params->temp_inoffset = delta;
				break;
			default:
				break;	// never happen
		}
	}

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "summer_maintenance", false, NULL, NULL, },	// 0
		{ NODEBOL, "logging", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_tsummer", false, NULL, NULL, },	// 2
		{ NODEFLT|NODEINT, "limit_tfrost", false, NULL, NULL, },
		{ NODEINT|NODEDUR, "sleeping_delay", false, NULL, NULL, },	// 4
		{ NODESTR, "startup_sysmode", true, NULL, NULL, },
		{ NODESTR, "startup_runmode", false, NULL, NULL, },	// 6
		{ NODESTR, "startup_dhwmode", false, NULL, NULL, },
		{ NODELST, "def_hcircuit", false, NULL, NULL, },	// 8
		{ NODELST, "def_dhwt", false, NULL, NULL, },
		{ NODEINT|NODEDUR, "summer_run_interval", false, NULL, NULL, },	// 10
		{ NODEINT|NODEDUR, "summer_run_duration", false, NULL, NULL, },
	};
	struct s_runtime * const runtime = priv;
	struct s_config * restrict config;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	temp_t celsius;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	config = config_new();
	if (!config)
		return (-EOOM);

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				config->summer_maintenance = currnode->value.boolval;
				break;
			case 1:
				config->logging = currnode->value.boolval;
				break;
			case 2:
			case 3:
				celsius = (NODEFLT == currnode->type) ? celsius_to_temp(currnode->value.floatval) : celsius_to_temp(currnode->value.intval);
				switch (i) {
					case 2:
						if (ALL_OK != config_set_tsummer(config, celsius))
							goto invaliddata;
						break;
					case 3:
						if (ALL_OK != config_set_tfrost(config, celsius))
							goto invaliddata;
						break;
					default:
						break;
				}
				break;
			case 4:
			case 10:
			case 11:
				// positive time values
				if (currnode->value.intval < 0)
					goto invaliddata;
				else
					switch (i) {
						case 4:
							config->sleeping_delay = timekeep_sec_to_tk(currnode->value.intval);
							break;
						case 10:
							config->summer_run_interval = timekeep_sec_to_tk(currnode->value.intval);
							break;
						case 11:
							config->summer_run_duration = timekeep_sec_to_tk(currnode->value.intval);
							break;
						default:
							break;
					}
				break;
			case 5:
				ret = sysmode_parse(&config->startup_sysmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 6:
				ret = filecfg_parser_runmode_parse(&config->startup_runmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 7:
				ret = filecfg_parser_runmode_parse(&config->startup_dhwmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 8:
				if (ALL_OK != hcircuit_params_parse(&config->def_hcircuit, currnode))
					goto invaliddata;
				break;
			case 9:
				if (ALL_OK != dhwt_params_parse(&config->def_dhwt, currnode))
					goto invaliddata;
				break;
			default:
				break;
		}
	}

	// consistency checks post matching

	if (SYS_MANUAL == config->startup_sysmode) {
		if (!parsers[6].node || !parsers[7].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: startup_sysmode set to \"manual\" but startup_runmode and/or startup_dhwmode are not set"), node->name, node->lineno);
			return (-EINVALID);
		}
	}

	if (config->summer_maintenance) {
		if (!parsers[10].node || !parsers[11].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: summer_maintenance is set but summer_run_interval and/or summer_run_duration are not set"), node->name, node->lineno);
			return (-EINVALID);

		}
	}
	config->configured = true;
	runtime->config = config;

	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
	return (ALL_OK);

	// we choose to interrupt parsing if an error occurs in this function, but let the subparsers run to the end
invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
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

FILECFG_PARSER_TIME_PARSE_FUNC(s_pump, cooldown_time)
FILECFG_PARSER_RID_PARSE_FUNC(s_pump, rid_pump)

static int pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"cooldown_time",	false,	fcp_tk_s_pump_cooldown_time,	NULL, },
		{ NODELST,		"rid_pump",		true,	fcp_rid_s_pump_rid_pump,	NULL, },
	};
	struct s_plant * restrict const plant = priv;
	struct s_pump * pump;
	int ret;

	// we receive a 'pump' node with a valid string attribute which is the pump name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the pump
	pump = plant_new_pump(plant, node->value.stringval);
	if (!pump)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(pump, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	pump->set.configured = true;

	return (ALL_OK);
}

static int pumps_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "pump", pump_parse));
}

FILECFG_PARSER_TIME_PARSE_FUNC(s_valve_sapprox_priv, sample_intvl)
FILECFG_PARSER_INT_PARSE_FUNC(true, s_valve_sapprox_priv, amount)

static int valve_algo_sapprox_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"sample_intvl",	true,	fcp_tk_s_valve_sapprox_priv_sample_intvl,	NULL, },
		{ NODEINT,		"amount",	true,	fcp_int_s_valve_sapprox_priv_amount,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	struct s_valve_sapprox_priv sapriv = { 0 };
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&sapriv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// XXX
	if ((sapriv.set.amount < 0) || (sapriv.set.amount > UINT_FAST8_MAX)) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: amount is out of range"), node->name, node->lineno);
		return -EINVALID;
	}

	ret = valve_make_sapprox(valve, sapriv.set.amount, sapriv.set.sample_intvl);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		default:	// should never happen
			dbgerr("valve_make_sapprox() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

FILECFG_PARSER_TIME_PARSE_FUNC(s_valve_pi_priv, sample_intvl)
FILECFG_PARSER_TIME_PARSE_FUNC(s_valve_pi_priv, Tu)
FILECFG_PARSER_TIME_PARSE_FUNC(s_valve_pi_priv, Td)
FILECFG_PARSER_INT_PARSE_FUNC(true, s_valve_pi_priv, tune_f)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, true, s_valve_pi_priv, Ksmax)

static int valve_algo_PI_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"sample_intvl",	true,	fcp_tk_s_valve_pi_priv_sample_intvl,	NULL, },
		{ NODEINT|NODEDUR,	"Tu",		true,	fcp_tk_s_valve_pi_priv_Tu,		NULL, },
		{ NODEINT|NODEDUR,	"Td",		true,	fcp_tk_s_valve_pi_priv_Td,		NULL, },
		{ NODEINT,		"tune_f",	true,	fcp_int_s_valve_pi_priv_tune_f,		NULL, },
		{ NODEFLT|NODEINT,	"Ksmax",	true,	fcp_temp_s_valve_pi_priv_Ksmax,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	struct s_valve_pi_priv pipriv = { 0 };
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&pipriv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// XXX
	if ((pipriv.set.tune_f < 0) || (pipriv.set.tune_f > UINT_FAST8_MAX)) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: tune_f is out of rnage"), node->name, node->lineno);
		return -EINVALID;
	}

	ret = valve_make_pi(valve, pipriv.set.sample_intvl, pipriv.set.Td, pipriv.set.Tu, pipriv.set.Ksmax, pipriv.set.tune_f);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		case -EMISCONFIGURED:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: incorrect values for sample_intvl '%d' vs Tu '%d'"), node->name, node->lineno, parsers[0].node->value.intval, parsers[1].node->value.intval);
			break;
		default:	// should never happen
			dbgerr("valve_make_sapprox() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int fcp_tid_valve_tmix_tid_hot(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_tid_parse(&valve->set.tset.tmix.tid_hot, node));
}

static int fcp_tid_valve_tmix_tid_cold(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_tid_parse(&valve->set.tset.tmix.tid_cold, node));
}

static int fcp_tid_valve_tmix_tid_out(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_tid_parse(&valve->set.tset.tmix.tid_out, node));
}

static int fcp_temp_valve_tmix_tdeadzone(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	int ret; temp_t temp = 0;
	ret = __fcp_get_node_celsius_temp(true, true, node, &temp);
	valve->set.tset.tmix.tdeadzone = temp;	/* Note: always set */
	return (ret);
}

static int fcp_valve_tmix_algo(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str;
	int ret;

	str = node->value.stringval;
	if (!strcmp("PI", str))
		ret = valve_algo_PI_parser(valve, node);
	else if (!strcmp("sapprox", str))
		ret = valve_algo_sapprox_parser(valve, node);
	else if (!strcmp("bangbang", str))
		ret = valve_make_bangbang(valve);
	else
		return (-EINVALID);

	return (ret);
}

static int valve_tmix_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"tdeadzone",	false,	fcp_temp_valve_tmix_tdeadzone,	NULL, },
		{ NODELST,		"tid_hot",	false,	fcp_tid_valve_tmix_tid_hot,	NULL, },
		{ NODELST,		"tid_cold",	false,	fcp_tid_valve_tmix_tid_cold,	NULL, },
		{ NODELST,		"tid_out",	true,	fcp_tid_valve_tmix_tid_out,	NULL, },
		{ NODESTR,		"algo",		true,	fcp_valve_tmix_algo,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	valve->set.type = VA_TYPE_MIX;	// needed by valve_make_* algos

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));

	return (ret);
}

static int fcp_bool_valve_tisol_reverse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	valve->set.tset.tisol.reverse = node->value.boolval;
	return (ALL_OK);
}

static int valve_tisol_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,	"reverse",	true,	fcp_bool_valve_tisol_reverse,	NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.type = VA_TYPE_ISOL;

	return (ret);
}

static int fcp_rid_valve_m3way_rid_open(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_rid_parse(&valve->set.mset.m3way.rid_open, node));
}

static int fcp_rid_valve_m3way_rid_close(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_rid_parse(&valve->set.mset.m3way.rid_close, node));
}

static int valve_m3way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"rid_open",	true,	fcp_rid_valve_m3way_rid_open,	NULL, },
		{ NODELST,	"rid_close",	true,	fcp_rid_valve_m3way_rid_close,	NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.motor = VA_M_3WAY;

	return (ALL_OK);
}

static int fcp_rid_valve_m2way_rid_trigger(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_parser_rid_parse(&valve->set.mset.m2way.rid_trigger, node));
}

static int fcp_bool_valve_m2way_trigger_opens(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	valve->set.mset.m2way.trigger_opens = node->value.boolval;
	return (ALL_OK);
}

static int valve_m2way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"rid_trigger",		true,	fcp_rid_valve_m2way_rid_trigger,	NULL, },
		{ NODEBOL,	"trigger_opens",	true,	fcp_bool_valve_m2way_trigger_opens,	NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.motor = VA_M_2WAY;

	return (ALL_OK);
}

FILECFG_PARSER_INT_PARSE_FUNC(true, s_valve, deadband)
FILECFG_PARSER_TIME_PARSE_FUNC(s_valve, ete_time)

static int fcp_valve_type(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str;
	int ret;

	str = node->value.stringval;
	if (!strcmp("mix", str))
		ret = valve_tmix_parser(valve, node);
	else if (!strcmp("isol", str))
		ret = valve_tisol_parser(valve, node);
	else
		return (-EINVALID);

	return (ret);
}

static int fcp_valve_motor(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str;
	int ret;

	str = node->value.stringval;
	if (!strcmp("3way", str))
		ret = valve_m3way_parser(valve, node);
	else if (!strcmp("2way", str))
		ret = valve_m2way_parser(valve, node);
	else
		return (-EINVALID);

	return (ret);
}

static int valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT,		"deadband",	false,	fcp_int_s_valve_deadband,	NULL, },
		{ NODEINT|NODEDUR,	"ete_time",	true,	fcp_tk_s_valve_ete_time,	NULL, },
		{ NODESTR,		"type",		true,	fcp_valve_type,			NULL, },
		{ NODESTR,		"motor",	true,	fcp_valve_motor,		NULL, },
	};
	struct s_plant * restrict const plant = priv;
	struct s_valve * valve;
	int ret;

	// we receive a 'valve' node with a valid string attribute which is the valve name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the valve
	valve = plant_new_valve(plant, node->value.stringval);
	if (!valve)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.configured = true;

	return (ALL_OK);
}

static int valves_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "valve", valve_parse));
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

FILECFG_PARSER_BOOL_PARSE_FUNC(s_dhwt, electric_failover)
FILECFG_PARSER_BOOL_PARSE_FUNC(s_dhwt, anti_legionella)
FILECFG_PARSER_BOOL_PARSE_FUNC(s_dhwt, legionella_recycle)
FILECFG_PARSER_BOOL_PARSE_FUNC(s_dhwt, electric_recycle)
FILECFG_PARSER_PRIO_PARSE_FUNC(s_dhwt, prio)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_dhwt, runmode)
FILECFG_PARSER_TID_PARSE_FUNC(s_dhwt, tid_bottom)
FILECFG_PARSER_TID_PARSE_FUNC(s_dhwt, tid_top)
FILECFG_PARSER_TID_PARSE_FUNC(s_dhwt, tid_win)
FILECFG_PARSER_TID_PARSE_FUNC(s_dhwt, tid_wout)
FILECFG_PARSER_RID_PARSE_FUNC(s_dhwt, rid_selfheater)
FILECFG_PARSER_SCHEDID_PARSE_FUNC(s_dhwt, schedid)

static int fcp_dhwt_cprio(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	const char * str;

	str = node->value.stringval;
	if (!strcmp("paralmax", str))
		dhwt->set.dhwt_cprio = DHWTP_PARALMAX;
	else if (!strcmp("paraldhw", str))
		dhwt->set.dhwt_cprio = DHWTP_PARALDHW;
	else if (!strcmp("slidmax", str))
		dhwt->set.dhwt_cprio = DHWTP_SLIDMAX;
	else if (!strcmp("sliddhw", str))
		dhwt->set.dhwt_cprio = DHWTP_SLIDDHW;
	else if (!strcmp("absolute", str))
		dhwt->set.dhwt_cprio = DHWTP_ABSOLUTE;
	else
		return (-EINVALID);

	return (ALL_OK);
}

static int fcp_dhwt_force_mode(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	const char * str;

	str = node->value.stringval;
	if (!strcmp("never", str))
		dhwt->set.force_mode = DHWTF_NEVER;
	else if (!strcmp("first", str))
		dhwt->set.force_mode = DHWTF_FIRST;
	else if (!strcmp("always", str))
		dhwt->set.force_mode = DHWTF_ALWAYS;
	else
		return (-EINVALID);

	return (ALL_OK);
}

static int fcp_dhwt_params(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	return (dhwt_params_parse(&dhwt->set.params, node));
}

static int dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "pump_feed", false, NULL, NULL, },		// 0
		{ NODESTR, "pump_recycle", false, NULL, NULL, },	// 1
		{ NODESTR, "valve_hwisol", false, NULL, NULL, },	// 2

		{ NODEBOL,	"electric_failover",	false,	fcp_bool_s_dhwt_electric_failover,	NULL, },
		{ NODEBOL,	"anti_legionella",	false,	fcp_bool_s_dhwt_anti_legionella,	NULL, },
		{ NODEBOL,	"legionella_recycle",	false,	fcp_bool_s_dhwt_legionella_recycle,	NULL, },
		{ NODEBOL,	"electric_recycle",	false,	fcp_bool_s_dhwt_electric_recycle,	NULL, },
		{ NODEINT,	"prio",			false,	fcp_prio_s_dhwt_prio,			NULL, },
		{ NODESTR,	"schedid",		false,	fcp_schedid_s_dhwt_schedid,		NULL, },
		{ NODESTR,	"runmode",		true,	fcp_runmode_s_dhwt_runmode,		NULL, },
		{ NODESTR,	"dhwt_cprio",		false,	fcp_dhwt_cprio,				NULL, },
		{ NODESTR,	"force_mode",		false,	fcp_dhwt_force_mode,			NULL, },
		{ NODELST,	"tid_bottom",		false,	fcp_tid_s_dhwt_tid_bottom,		NULL, },
		{ NODELST,	"tid_top",		false,	fcp_tid_s_dhwt_tid_top,			NULL, },
		{ NODELST,	"tid_win",		false,	fcp_tid_s_dhwt_tid_win,			NULL, },
		{ NODELST,	"tid_wout",		false,	fcp_tid_s_dhwt_tid_wout,		NULL, },
		{ NODELST,	"rid_selfheater",	false,	fcp_rid_s_dhwt_rid_selfheater,		NULL, },
		{ NODELST,	"params",		false,	fcp_dhwt_params,			NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_pump * pump;
	struct s_plant * restrict const plant = priv;
	struct s_dhwt * dhwt;
	const char * n;
	int ret;
	unsigned int i;

	// we receive a 'dhwt' node with a valid string attribute which is the dhwt name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the dhwt
	dhwt = plant_new_dhwt(plant, node->value.stringval);
	if (!dhwt)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(dhwt, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// deal with the pointers (XXX TODO: make that work with regular parsers)
	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (parsers[i].parser)
			continue;

		currnode = parsers[i].node;
		if (!currnode)
			continue;

		// we only deal with stringvals
		n = currnode->value.stringval;
		if (strlen(n) < 1)
			break;	// nothing to do

		switch (i) {
			case 0:
			case 1:
				pump = plant_fbn_pump(plant, n);
				if (!pump)
					goto invaliddata;	// pump not found
				if (0 == i)
					dhwt->set.p.pump_feed = pump;
				else	// i == 1
					dhwt->set.p.pump_recycle = pump;
				break;

			case 2:
				dhwt->set.p.valve_hwisol = plant_fbn_valve(plant, n);
				if (!dhwt->set.p.valve_hwisol)
					goto invaliddata;
				break;
			default:
				break;	// should never happen
		}
	}

	dhwt->set.configured = true;

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "dhwt", dhwt_parse));
}

struct s_fcp_tlbilin_params {
	struct {
		temp_t tout1;
		temp_t twater1;
		temp_t tout2;
		temp_t twater2;
		int nH100;
	} set;
};

FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_fcp_tlbilin_params, tout1)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_fcp_tlbilin_params, twater1)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_fcp_tlbilin_params, tout2)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_fcp_tlbilin_params, twater2)
FILECFG_PARSER_INT_PARSE_FUNC(false, s_fcp_tlbilin_params, nH100)

static int hcircuit_tlaw_bilinear_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"tout1",	true,	fcp_temp_s_fcp_tlbilin_params_tout1,	NULL, },
		{ NODEFLT|NODEINT,	"twater1",	true,	fcp_temp_s_fcp_tlbilin_params_twater1,	NULL, },
		{ NODEFLT|NODEINT,	"tout2",	true,	fcp_temp_s_fcp_tlbilin_params_tout2,	NULL, },
		{ NODEFLT|NODEINT,	"twater2",	true,	fcp_temp_s_fcp_tlbilin_params_twater2,	NULL, },
		{ NODEINT,		"nH100",	true,	fcp_int_s_fcp_tlbilin_params_nH100,	NULL, },
	};
	struct s_hcircuit * restrict const hcircuit = priv;
	struct s_fcp_tlbilin_params p;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&p, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = hcircuit_make_bilinear(hcircuit, p.set.tout1, p.set.twater1, p.set.tout2, p.set.twater2, p.set.nH100);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		default:	// should never happen
			dbgerr("hcircuit_make_bilinear() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

FILECFG_PARSER_BOOL_PARSE_FUNC(s_hcircuit, fast_cooldown)
FILECFG_PARSER_BOOL_PARSE_FUNC(s_hcircuit, logging)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_hcircuit, runmode)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, true, s_hcircuit, wtemp_rorh)
FILECFG_PARSER_TIME_PARSE_FUNC(s_hcircuit, am_tambient_tK)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit, tambient_boostdelta)
FILECFG_PARSER_TIME_PARSE_FUNC(s_hcircuit, boost_maxtime)
FILECFG_PARSER_TID_PARSE_FUNC(s_hcircuit, tid_outgoing)
FILECFG_PARSER_TID_PARSE_FUNC(s_hcircuit, tid_return)
FILECFG_PARSER_TID_PARSE_FUNC(s_hcircuit, tid_ambient)
FILECFG_PARSER_SCHEDID_PARSE_FUNC(s_hcircuit, schedid)
FILECFG_PARSER_PBMODEL_PARSE_FUNC(s_hcircuit, bmodel)

static int fcp_hcircuit_params(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hcircuit * restrict const hcircuit = priv;
	return (hcircuit_params_parse(&hcircuit->set.params, node));
}

static int fcp_hcircuit_tlaw(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hcircuit * restrict const hcircuit = priv;
	const char * str = node->value.stringval;

	if (!strcmp("bilinear", str))
		return (hcircuit_tlaw_bilinear_parser(hcircuit, node));
	else
		return (-EINVALID);
}

static int fcp_hcircuit_ambient_factor(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hcircuit * restrict const hcircuit = priv;
	int iv = node->value.intval;

	if (abs(iv) > 100)
		return (-EINVALID);
	hcircuit->set.ambient_factor = iv;
	return (ALL_OK);
}

static int hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "valve_mix", false, NULL, NULL, },	// 0
		{ NODESTR, "pump_feed", false, NULL, NULL, },	// 1

		{ NODEBOL,		"fast_cooldown",	false,	fcp_bool_s_hcircuit_fast_cooldown,	NULL, },
		{ NODEBOL,		"logging",		false,	fcp_bool_s_hcircuit_logging,		NULL, },
		{ NODESTR,		"runmode",		true,	fcp_runmode_s_hcircuit_runmode,		NULL, },
		{ NODESTR,		"schedid",		false,	fcp_schedid_s_hcircuit_schedid,		NULL, },
		{ NODEINT,		"ambient_factor",	false,	fcp_hcircuit_ambient_factor,		NULL, },
		{ NODEFLT|NODEINT,	"wtemp_rorh",		false,	fcp_temp_s_hcircuit_wtemp_rorh,		NULL, },
		{ NODEINT|NODEDUR,	"am_tambient_tK",	false,	fcp_tk_s_hcircuit_am_tambient_tK,	NULL, },
		{ NODEFLT|NODEINT,	"tambient_boostdelta",	false,	fcp_temp_s_hcircuit_tambient_boostdelta, NULL, },
		{ NODEINT|NODEDUR,	"boost_maxtime",	false,	fcp_tk_s_hcircuit_boost_maxtime,	NULL, },
		{ NODELST,		"tid_outgoing",		true,	fcp_tid_s_hcircuit_tid_outgoing,	NULL, },
		{ NODELST,		"tid_return",		false,	fcp_tid_s_hcircuit_tid_return,		NULL, },
		{ NODELST,		"tid_ambient",		false,	fcp_tid_s_hcircuit_tid_ambient,		NULL, },
		{ NODELST,		"params",		false,	fcp_hcircuit_params,			NULL, },
		{ NODESTR,		"tlaw",			true,	fcp_hcircuit_tlaw,			NULL, },
		{ NODESTR,		"bmodel",		true,	fcp_bmodel_s_hcircuit_pbmodel,		NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_hcircuit * hcircuit;
	const char * n;
	int ret;
	unsigned int i;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the hcircuit
	hcircuit = plant_new_circuit(plant, node->value.stringval);
	if (!hcircuit)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(hcircuit, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// deal with the pointers (XXX TODO: make that work with regular parsers)
	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (parsers[i].parser)
			continue;

		currnode = parsers[i].node;
		if (!currnode)
			continue;

		// we only deal with stringvals
		n = currnode->value.stringval;
		if (strlen(n) < 1)
			break;	// nothing to do

		switch (i) {
			case 0:
				hcircuit->set.p.valve_mix = plant_fbn_valve(plant, n);
				if (!hcircuit->set.p.valve_mix)
					goto invaliddata;
				break;
			case 1:
				hcircuit->set.p.pump_feed = plant_fbn_pump(plant, n);
				if (!hcircuit->set.p.pump_feed)
					goto invaliddata;
				break;
			default:
				break;	// should never happen
		}
	}

	hcircuit->set.configured = true;

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", hcircuit_parse));
}

#include "boiler.h"

FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, true, s_boiler_priv, hysteresis)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, false, s_boiler_priv, limit_thardmax)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, false, s_boiler_priv, limit_tmax)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, false, s_boiler_priv, limit_tmin)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, false, s_boiler_priv, limit_treturnmin)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, false, s_boiler_priv, t_freeze)
FILECFG_PARSER_TIME_PARSE_FUNC(s_boiler_priv, burner_min_time)
FILECFG_PARSER_TID_PARSE_FUNC(s_boiler_priv, tid_boiler)
FILECFG_PARSER_TID_PARSE_FUNC(s_boiler_priv, tid_boiler_return)
FILECFG_PARSER_RID_PARSE_FUNC(s_boiler_priv, rid_burner_1)
FILECFG_PARSER_RID_PARSE_FUNC(s_boiler_priv, rid_burner_2)

static int fcp_hs_boiler_idle_mode(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_boiler_priv * restrict const boiler = priv;
	const char * str;

	str = node->value.stringval;
	if (!strcmp("never", str))
		boiler->set.idle_mode = IDLE_NEVER;
	else if (!strcmp("frostonly", str))
		boiler->set.idle_mode = IDLE_FROSTONLY;
	else if (!strcmp("always", str))
		boiler->set.idle_mode = IDLE_ALWAYS;
	else
		return (-EINVALID);

	return (ALL_OK);
}

static int hs_boiler_parse(const struct s_plant * const plant, struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "pump_load", false, NULL, NULL, },		// 0
		{ NODESTR, "valve_ret", false, NULL, NULL, },		// 1

		{ NODESTR,		"idle_mode",		false,	fcp_hs_boiler_idle_mode,		NULL, },
		{ NODEFLT|NODEINT,	"hysteresis",		true,	fcp_temp_s_boiler_priv_hysteresis,	NULL, },
		{ NODEFLT|NODEINT,	"limit_thardmax",	true,	fcp_temp_s_boiler_priv_limit_thardmax,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmax",		false,	fcp_temp_s_boiler_priv_limit_tmax,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmin",		false,	fcp_temp_s_boiler_priv_limit_tmin,	NULL, },
		{ NODEFLT|NODEINT,	"limit_treturnmin",	false,	fcp_temp_s_boiler_priv_limit_treturnmin, NULL, },
		{ NODEFLT|NODEINT,	"t_freeze",		true,	fcp_temp_s_boiler_priv_t_freeze,	NULL, },
		{ NODEINT|NODEDUR,	"burner_min_time",	false,	fcp_tk_s_boiler_priv_burner_min_time,	NULL, },
		{ NODELST,		"tid_boiler",		true,	fcp_tid_s_boiler_priv_tid_boiler,	NULL, },
		{ NODELST,		"tid_boiler_return",	false,	fcp_tid_s_boiler_priv_tid_boiler_return, NULL, },
		{ NODELST,		"rid_burner_1",		true,	fcp_rid_s_boiler_priv_rid_burner_1,	NULL, },
		{ NODELST,		"rid_burner_2",		false,	fcp_rid_s_boiler_priv_rid_burner_2,	NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_boiler_priv * boiler;
	const char * n;
	int ret;
	unsigned int i;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// make that heatsource a boiler
	ret = boiler_heatsource(heatsource);
	if (ret)
		return (ret);

	// configure that boiler
	boiler = heatsource->priv;

	ret = filecfg_parser_run_parsers(boiler, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// deal with the pointers (XXX TODO: make that work with regular parsers)
	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (parsers[i].parser)
			continue;

		currnode = parsers[i].node;
		if (!currnode)
			continue;

		// we only deal with stringvals
		n = currnode->value.stringval;
		if (strlen(n) < 1)
			continue;	// nothing to do

		switch (i) {
			case 0:
				boiler->set.p.pump_load = plant_fbn_pump(plant, n);
				if (!boiler->set.p.pump_load)
					goto invaliddata;
				break;
			case 1:
				boiler->set.p.valve_ret = plant_fbn_valve(plant, n);
				if (!boiler->set.p.valve_ret)
					goto invaliddata;
				break;
			default:
				break;	// should never happen
		}
	}

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);

}

static int heatsource_type_parse(const struct s_plant * const plant, struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	int ret;

	if (!strcmp("boiler", node->value.stringval))
		ret = hs_boiler_parse(plant, heatsource, node);
	else
		ret = -EUNKNOWN;

	return (ret);
}

FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_heatsource, runmode)
FILECFG_PARSER_PRIO_PARSE_FUNC(s_heatsource, prio)
FILECFG_PARSER_TIME_PARSE_FUNC(s_heatsource, consumer_sdelay)
FILECFG_PARSER_SCHEDID_PARSE_FUNC(s_heatsource, schedid)

static int heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "type", true, NULL, NULL, },		// 0

		{ NODESTR,		"runmode",		true,	fcp_runmode_s_heatsource_runmode,	NULL, },
		{ NODEINT,		"prio",			false,	fcp_prio_s_heatsource_prio,		NULL, },
		{ NODEINT|NODEDUR,	"consumer_sdelay",	false,	fcp_tk_s_heatsource_consumer_sdelay,	NULL, },
		{ NODESTR,		"schedid",		false,	fcp_schedid_s_heatsource_schedid,	NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_heatsource * heatsource;
	int ret;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the heatsource
	heatsource = plant_new_heatsource(plant, node->value.stringval);
	if (!heatsource)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(heatsource, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// deal with the heatsource type (XXX TODO: make that work with regular parsers)
	currnode = parsers[0].node;
	ret = heatsource_type_parse(plant, heatsource, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	heatsource->set.configured = true;

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int heatsources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "heatsource", heatsource_parse));
}

static int plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
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
	if (ALL_OK == ret)
		plant->configured = true;

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
		{ NODELST,	"scheduler",	false,	scheduler_filecfg_parse, NULL, },	// we need schedulers during plant setup
		{ NODELST,	"defconfig",	false,	defconfig_parse,	NULL, },
		{ NODELST,	"models",	false,	models_filecfg_parse,	NULL, },
		{ NODELST,	"plant",	true,	plant_parse,		NULL, },
		{ NODELST,	"storage",	false,	storage_filecfg_parse,	NULL, },
		{ NODELST,	"logging",	false,	log_filecfg_parse,	NULL, },
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
