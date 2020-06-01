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
#include "dhwt.h"
#include "hcircuit.h"
#include "heatsource.h"

#include "scheduler.h"
#include "scheduler_filecfg.h"

#include "models_filecfg.h"
#include "storage_filecfg.h"
#include "log_filecfg.h"

#include "runtime.h"

#ifndef ARRAY_SIZE
 #define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))
#endif

#define container_of(ptr, type, member) ({					\
	const typeof( ((type *)0)->member )					\
	*__mptr = (ptr);							\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define pdata_to_plant(_pdata)	container_of(_pdata, struct s_plant, pdata)

#define FILECFG_PARSER_STR_PARSE_SET_FUNC(_nonempty, _struct, _setmember)		\
static int fcp_str_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	const char *str = n->value.stringval;					\
	if (_nonempty && (strlen(str) < 1))					\
		return (-EINVALID);						\
	s->set._setmember = str;						\
	return (ALL_OK);							\
}

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

#define FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, _nest, _member)		\
static int fcp_runmode_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_parser_runmode_parse(&s->_nest _member, n));		\
}

#define FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(_struct, _setmember)		\
	FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_RUNMODE_PARSE_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, , _setmember)

#define FILECFG_PARSER_PRIO_PARSE_SET_FUNC(_struct, _setmember)			\
static int fcp_prio_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if ((iv < 0) || (iv > UINT_FAST8_MAX))					\
		return (-EINVALID);						\
	s->set._setmember = (typeof(s->set._setmember))iv;			\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(_struct, _setmember)			\
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

#define FILECFG_PARSER_PBMODEL_PARSE_SET_FUNC(_struct, _setpmember)			\
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

#define FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(_priv2plant, _struct, _setpmember)		\
static int fcp_pump_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = plant_fbn_pump(_priv2plant(priv), n->value.stringval);	\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(_priv2plant, _struct, _setpmember)		\
static int fcp_valve_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = plant_fbn_valve(_priv2plant(priv), n->value.stringval);	\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
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

FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, t_comfort)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, t_eco)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, t_frostfree)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, t_legionella)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, limit_tmin)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, limit_tmax)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_dhwt_params, limit_wintmax)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, true, s_dhwt_params, hysteresis)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, true, s_dhwt_params, temp_inoffset)
FILECFG_PARSER_TIME_PARSE_FUNC(s_dhwt_params, limit_chargetime)

static int dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"t_comfort",		false,	fcp_temp_s_dhwt_params_t_comfort,	NULL, },
		{ NODEFLT|NODEINT,	"t_eco",		false,	fcp_temp_s_dhwt_params_t_eco,		NULL, },
		{ NODEFLT|NODEINT,	"t_frostfree",		false,	fcp_temp_s_dhwt_params_t_frostfree,	NULL, },
		{ NODEFLT|NODEINT,	"t_legionella",		false,	fcp_temp_s_dhwt_params_t_legionella,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmin",		false,	fcp_temp_s_dhwt_params_limit_tmin,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmax",		false,	fcp_temp_s_dhwt_params_limit_tmax,	NULL, },
		{ NODEFLT|NODEINT,	"limit_wintmax",	false,	fcp_temp_s_dhwt_params_limit_wintmax,	NULL, },
		{ NODEFLT|NODEINT,	"hysteresis",		false,	fcp_temp_s_dhwt_params_hysteresis,	NULL, },
		{ NODEFLT|NODEINT,	"temp_inoffset",	false,	fcp_temp_s_dhwt_params_temp_inoffset,	NULL, },
		{ NODEINT|NODEDUR,	"limit_chargetime",	false,	fcp_tk_s_dhwt_params_limit_chargetime,	NULL, },
	};

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}

FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, t_comfort)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, t_eco)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, t_frostfree)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, t_offset)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, outhoff_comfort)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, outhoff_eco)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, outhoff_frostfree)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(true, true, s_hcircuit_params, outhoff_hysteresis)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, limit_wtmin)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_hcircuit_params, limit_wtmax)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, true, s_hcircuit_params, temp_inoffset)

static int hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"t_comfort",		false,	fcp_temp_s_hcircuit_params_t_comfort,		NULL, },
		{ NODEFLT|NODEINT,	"t_eco",		false,	fcp_temp_s_hcircuit_params_t_eco,		NULL, },
		{ NODEFLT|NODEINT,	"t_frostfree",		false,	fcp_temp_s_hcircuit_params_t_frostfree,		NULL, },
		{ NODEFLT|NODEINT,	"t_offset",		false,	fcp_temp_s_hcircuit_params_t_offset,		NULL, },
		{ NODEFLT|NODEINT,	"outhoff_comfort",	false,	fcp_temp_s_hcircuit_params_outhoff_comfort,	NULL, },
		{ NODEFLT|NODEINT,	"outhoff_eco",		false,	fcp_temp_s_hcircuit_params_outhoff_eco,		NULL, },
		{ NODEFLT|NODEINT,	"outhoff_frostfree",	false,	fcp_temp_s_hcircuit_params_outhoff_frostfree,	NULL, },
		{ NODEFLT|NODEINT,	"outhoff_hysteresis",	false,	fcp_temp_s_hcircuit_params_outhoff_hysteresis,	NULL, },
		{ NODEFLT|NODEINT,	"limit_wtmin",		false,	fcp_temp_s_hcircuit_params_limit_wtmin,		NULL, },
		{ NODEFLT|NODEINT,	"limit_wtmax",		false,	fcp_temp_s_hcircuit_params_limit_wtmax,		NULL, },
		{ NODEFLT|NODEINT,	"temp_inoffset",	false,	fcp_temp_s_hcircuit_params_temp_inoffset,	NULL, },
	};

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}

FILECFG_PARSER_BOOL_PARSE_FUNC(s_config, summer_maintenance)
FILECFG_PARSER_BOOL_PARSE_FUNC(s_config, logging)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_config, limit_tsummer)
FILECFG_PARSER_CELSIUS_PARSE_FUNC(false, false, s_config, limit_tfrost)
FILECFG_PARSER_TIME_PARSE_FUNC(s_config, sleeping_delay)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_config, startup_runmode)
FILECFG_PARSER_RUNMODE_PARSE_FUNC(s_config, startup_dhwmode)
FILECFG_PARSER_TIME_PARSE_FUNC(s_config, summer_run_interval)
FILECFG_PARSER_TIME_PARSE_FUNC(s_config, summer_run_duration)

static int defconfig_sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	return (sysmode_parse(&config->startup_sysmode, node));
}

static int defconfig_def_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	return (hcircuit_params_parse(&config->def_hcircuit, node));
}

static int defconfig_def_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	return (dhwt_params_parse(&config->def_dhwt, node));
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"summer_maintenance",	false,	fcp_bool_s_config_summer_maintenance,	NULL, },	// 0
		{ NODEBOL,		"logging",		false,	fcp_bool_s_config_logging,		NULL, },
		{ NODEFLT|NODEINT,	"limit_tsummer",	false,	fcp_temp_s_config_limit_tsummer,	NULL, },	// 2
		{ NODEFLT|NODEINT,	"limit_tfrost",		false,	fcp_temp_s_config_limit_tfrost,		NULL, },
		{ NODEINT|NODEDUR,	"sleeping_delay",	false,	fcp_tk_s_config_sleeping_delay,		NULL, },	// 4
		{ NODESTR,		"startup_sysmode",	true,	defconfig_sysmode_parse,		NULL, },
		{ NODESTR,		"startup_runmode",	false,	fcp_runmode_s_config_startup_runmode,	NULL, },	// 6
		{ NODESTR,		"startup_dhwmode",	false,	fcp_runmode_s_config_startup_dhwmode,	NULL, },
		{ NODELST,		"def_hcircuit",		false,	defconfig_def_hcircuit_parse,		NULL, },	// 8
		{ NODELST,		"def_dhwt",		false,	defconfig_def_dhwt_parse,		NULL, },
		{ NODEINT|NODEDUR,	"summer_run_interval",	false,	fcp_tk_s_config_summer_run_interval,	NULL, },	// 10
		{ NODEINT|NODEDUR,	"summer_run_duration",	false,	fcp_tk_s_config_summer_run_duration,	NULL, },
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
		if (!parsers[6].node || !parsers[7].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: startup_sysmode set to \"manual\" but startup_runmode and/or startup_dhwmode are not set"), node->name, node->lineno);
			return (-EINVALID);
		}
	}

	if (config->summer_maintenance) {
		if (config->summer_run_interval || config->summer_run_duration) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: summer_maintenance is set but summer_run_interval and/or summer_run_duration are not set"), node->name, node->lineno);
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

static inline const struct s_plant * __dhwt_to_plant(void * priv)
{
	return (pdata_to_plant(((struct s_dhwt *)priv)->pdata));
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, electric_failover)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, anti_legionella)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, legionella_recycle)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, electric_recycle)
FILECFG_PARSER_PRIO_PARSE_SET_FUNC(s_dhwt, prio)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_dhwt, runmode)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_dhwt, tid_bottom)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_dhwt, tid_top)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_dhwt, tid_win)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_dhwt, tid_wout)
FILECFG_PARSER_RID_PARSE_SET_FUNC(s_dhwt, rid_selfheater)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_dhwt, schedid)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, pump_feed)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, pump_recycle)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, valve_hwisol)

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
		{ NODESTR,	"pump_feed",		false,	fcp_pump_s_dhwt_ppump_feed,		NULL, },
		{ NODESTR,	"pump_recycle",		false,	fcp_pump_s_dhwt_ppump_recycle,		NULL, },
		{ NODESTR,	"valve_hwisol",		false,	fcp_valve_s_dhwt_pvalve_hwisol,		NULL, },
	};
	struct s_plant * restrict const plant = priv;
	struct s_dhwt * dhwt;
	int ret;

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

	dhwt->set.configured = true;

	return (ALL_OK);
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

FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_fcp_tlbilin_params, tout1)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_fcp_tlbilin_params, twater1)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_fcp_tlbilin_params, tout2)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_fcp_tlbilin_params, twater2)
FILECFG_PARSER_INT_PARSE_SET_FUNC(false, s_fcp_tlbilin_params, nH100)

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

static inline const struct s_plant * __hcircuit_to_plant(void * priv)
{
	return (pdata_to_plant(((struct s_hcircuit *)priv)->pdata));
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_hcircuit, fast_cooldown)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_hcircuit, logging)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_hcircuit, runmode)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_hcircuit, wtemp_rorh)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_hcircuit, am_tambient_tK)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_hcircuit, tambient_boostdelta)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_hcircuit, boost_maxtime)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_hcircuit, tid_outgoing)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_hcircuit, tid_return)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_hcircuit, tid_ambient)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_hcircuit, schedid)
FILECFG_PARSER_PBMODEL_PARSE_SET_FUNC(s_hcircuit, bmodel)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__hcircuit_to_plant, s_hcircuit, pump_feed)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__hcircuit_to_plant, s_hcircuit, valve_mix)

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
		{ NODESTR,		"valve_mix",		false,	fcp_valve_s_hcircuit_pvalve_mix,	NULL, },
		{ NODESTR,		"pump_feed",		false,	fcp_pump_s_hcircuit_ppump_feed,		NULL, },
		{ NODESTR,		"bmodel",		true,	fcp_bmodel_s_hcircuit_pbmodel,		NULL, },
	};
	struct s_plant * restrict const plant = priv;
	struct s_hcircuit * hcircuit;
	int ret;

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

	hcircuit->set.configured = true;

	return (ALL_OK);
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", hcircuit_parse));
}

#include "boiler.h"

FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_boiler_priv, hysteresis)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_thardmax)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_tmax)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_tmin)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_treturnmin)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, t_freeze)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_boiler_priv, burner_min_time)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_boiler_priv, tid_boiler)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_boiler_priv, tid_boiler_return)
FILECFG_PARSER_RID_PARSE_SET_FUNC(s_boiler_priv, rid_burner_1)
FILECFG_PARSER_RID_PARSE_SET_FUNC(s_boiler_priv, rid_burner_2)

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

#define hspriv_to_heatsource(_priv)	container_of(_priv, struct s_heatsource, priv)

static inline const struct s_plant * __hspriv_to_plant(void * priv)
{
	return (pdata_to_plant(hspriv_to_heatsource(priv)->pdata));
}

FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__hspriv_to_plant, s_boiler_priv, pump_load)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__hspriv_to_plant, s_boiler_priv, valve_ret)

static int hs_boiler_parse(struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
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
		{ NODESTR,		"pump_load",		false,	fcp_pump_s_boiler_priv_ppump_load,	NULL, },
		{ NODESTR,		"valve_ret",		false,	fcp_valve_s_boiler_priv_pvalve_ret,	NULL, },
	};
	struct s_boiler_priv * boiler;
	int ret;

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
	return (ret);
}

static int heatsource_type_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_heatsource * const heatsource = priv;
	int ret;

	if (!strcmp("boiler", node->value.stringval))
		ret = hs_boiler_parse(heatsource, node);
	else
		ret = -EUNKNOWN;

	return (ret);
}

FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_heatsource, runmode)
FILECFG_PARSER_PRIO_PARSE_SET_FUNC(s_heatsource, prio)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_heatsource, consumer_sdelay)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_heatsource, schedid)

static int heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,		"type",			true,	heatsource_type_parse,			NULL, },
		{ NODESTR,		"runmode",		true,	fcp_runmode_s_heatsource_runmode,	NULL, },
		{ NODEINT,		"prio",			false,	fcp_prio_s_heatsource_prio,		NULL, },
		{ NODEINT|NODEDUR,	"consumer_sdelay",	false,	fcp_tk_s_heatsource_consumer_sdelay,	NULL, },
		{ NODESTR,		"schedid",		false,	fcp_schedid_s_heatsource_schedid,	NULL, },
	};
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

	heatsource->set.configured = true;

	return (ALL_OK);
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
