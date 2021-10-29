//
//  filecfg/parse/hcircuit_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit file configuration parsing.
 *
\verbatim
 hcircuit "name" {
	 log yes;
 	 fast_cooldown no;
	 runmode "auto";
	 schedid "default";
	 wtemp_rorh 25.0;
	 tambient_boostdelta 2.0;
	 boost_maxtime 4h;
 	 ambient_factor 20;
	 tid_outgoing "circuit out";
	 tid_return "circuit return";
 	 tid_ambient "ambient";
	 tlaw "bilinear" {
		 tout1 -5.0;
		 twater1 42.0;
		 tout2 15.0;
		 twater2 23.5;
		 nH100 110;
	 };
 	 params { ... };
	 valve_mix "circuit mix";
	 pump_feed "circuit pump";
	 bmodel "house";
 };
\endverbatim
 */

#include <stdlib.h>	// abs
#include <string.h>	// strlen/strcmp

#include "inputs_parse.h"
#include "hcircuit_parse.h"
#include "filecfg_parser.h"
#include "plant/hcircuit.h"
#include "plant/hcircuit_priv.h"

#include "scheduler.h"
#include "plant/plant.h"
#include "plant/pump.h"
#include "models.h"


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

int filecfg_hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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

	filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}


FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_tlaw_bilin20C_priv, tout1)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_tlaw_bilin20C_priv, twater1)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_tlaw_bilin20C_priv, tout2)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_tlaw_bilin20C_priv, twater2)
FILECFG_PARSER_INTPOSMAX_PARSE_SET_FUNC(200, s_tlaw_bilin20C_priv, nH100)

static int hcircuit_tlaw_bilinear_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"tout1",	true,	fcp_temp_s_tlaw_bilin20C_priv_tout1,	NULL, },
		{ NODEFLT|NODEINT,	"twater1",	true,	fcp_temp_s_tlaw_bilin20C_priv_twater1,	NULL, },
		{ NODEFLT|NODEINT,	"tout2",	true,	fcp_temp_s_tlaw_bilin20C_priv_tout2,	NULL, },
		{ NODEFLT|NODEINT,	"twater2",	true,	fcp_temp_s_tlaw_bilin20C_priv_twater2,	NULL, },
		{ NODEINT,		"nH100",	true,	fcp_int_s_tlaw_bilin20C_priv_nH100,	NULL, },
	};
	struct s_hcircuit * restrict const hcircuit = priv;
	struct s_tlaw_bilin20C_priv p;
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

#include "plant/plant_priv.h"
static inline const struct s_plant * __hcircuit_to_plant(void * priv)
{
	return (pdata_to_plant(((struct s_hcircuit *)priv)->pdata));
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_hcircuit, fast_cooldown)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_hcircuit, log)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_hcircuit, runmode)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_hcircuit, wtemp_rorh)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_hcircuit, tambient_boostdelta)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_hcircuit, boost_maxtime)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_hcircuit, tid_outgoing)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_hcircuit, tid_return)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_hcircuit, tid_ambient)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_hcircuit, schedid)
FILECFG_PARSER_PBMODEL_PARSE_SET_FUNC(s_hcircuit, bmodel)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__hcircuit_to_plant, s_hcircuit, pump_feed)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__hcircuit_to_plant, s_hcircuit, valve_mix)

static int fcp_hcircuit_params(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hcircuit * restrict const hcircuit = priv;
	return (filecfg_hcircuit_params_parse(&hcircuit->set.params, node));
}

static int fcp_hcircuit_tlaw(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hcircuit * restrict const hcircuit = priv;
	const char * str = node->value.stringval;

	if (!strcmp(str, "bilinear"))
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
	hcircuit->set.ambient_factor = (typeof(hcircuit->set.ambient_factor))iv;
	return (ALL_OK);
}

int filecfg_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"fast_cooldown",	false,	fcp_bool_s_hcircuit_fast_cooldown,	NULL, },
		{ NODEBOL,		"log",			false,	fcp_bool_s_hcircuit_log,		NULL, },
		{ NODESTR,		"runmode",		true,	fcp_runmode_s_hcircuit_runmode,		NULL, },
		{ NODESTR,		"schedid",		false,	fcp_schedid_s_hcircuit_schedid,		NULL, },
		{ NODEINT,		"ambient_factor",	false,	fcp_hcircuit_ambient_factor,		NULL, },
		{ NODEFLT|NODEINT,	"wtemp_rorh",		false,	fcp_temp_s_hcircuit_wtemp_rorh,		NULL, },
		{ NODEFLT|NODEINT,	"tambient_boostdelta",	false,	fcp_temp_s_hcircuit_tambient_boostdelta, NULL, },
		{ NODEINT|NODEDUR,	"boost_maxtime",	false,	fcp_tk_s_hcircuit_boost_maxtime,	NULL, },
		{ NODESTR,		"tid_outgoing",		true,	fcp_inputs_temperature_s_hcircuit_tid_outgoing,	NULL, },
		{ NODESTR,		"tid_return",		false,	fcp_inputs_temperature_s_hcircuit_tid_return,	NULL, },
		{ NODESTR,		"tid_ambient",		false,	fcp_inputs_temperature_s_hcircuit_tid_ambient,	NULL, },
		{ NODELST,		"params",		false,	fcp_hcircuit_params,			NULL, },
		{ NODESTC,		"tlaw",			true,	fcp_hcircuit_tlaw,			NULL, },
		{ NODESTR,		"valve_mix",		false,	fcp_valve_s_hcircuit_pvalve_mix,	NULL, },
		{ NODESTR,		"pump_feed",		false,	fcp_pump_s_hcircuit_ppump_feed,		NULL, },
		{ NODESTR,		"bmodel",		true,	fcp_bmodel_s_hcircuit_pbmodel,		NULL, },
	};
	struct s_hcircuit * restrict const hcircuit = priv;
	int ret;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	hcircuit->name = strdup(node->value.stringval);
	if (!hcircuit->name)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(hcircuit, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	hcircuit->set.configured = true;

	return (ALL_OK);
}
