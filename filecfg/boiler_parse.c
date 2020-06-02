//
//  filecfg/boiler_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler heatsource file configuration parsing.
 */

#include "boiler_parse.h"
#include "boiler.h"
#include "filecfg_parser.h"
#include "heatsource_parse.h"

#include "plant.h"

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

FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__hspriv_to_plant, s_boiler_priv, pump_load)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__hspriv_to_plant, s_boiler_priv, valve_ret)

int hs_boiler_parse(struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
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