//
//  filecfg/parse/boiler_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler heatsource file configuration parsing.
 *
\verbatim
 type "boiler" {
	 idle_mode "frostonly";
	 hysteresis 8.0;
	 limit_thardmax 100.0;
	 limit_tmax 90.0;
	 limit_tmin 50.0;
	 limit_treturnmin 35.0;
	 t_freeze 5.0;
	 burner_min_time 120;
	 tid_boiler "boiler";
	 rid_burner_1 "burner";
 };
\endverbatim
 */

#include "boiler_parse.h"
#include "filecfg_parser.h"
#include "heatsource_parse.h"
#include "inputs_parse.h"
#include "outputs_parse.h"

#include "plant/plant.h"
#include "plant/pump.h"
#include "plant/heatsources/boiler.h"
#include "plant/heatsource_priv.h"

FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_boiler_priv, hysteresis)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_thardmax)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_tmax)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_tmin)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, limit_treturnmin)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_boiler_priv, t_freeze)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_boiler_priv, burner_min_time)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_boiler_priv, tid_boiler)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_boiler_priv, tid_boiler_return)
FILECFG_OUTPUTS_PARSER_RELAY_PARSE_SET_FUNC(s_boiler_priv, rid_burner_1)
FILECFG_OUTPUTS_PARSER_RELAY_PARSE_SET_FUNC(s_boiler_priv, rid_burner_2)

const char * idle_mode_str[] = {
	[IDLE_NEVER]		= "never",
	[IDLE_FROSTONLY]	= "frostonly",
	[IDLE_ALWAYS]		= "always",
};

FILECFG_PARSER_ENUM_PARSE_SET_FUNC(idle_mode_str, s_boiler_priv, idle_mode)

#include "runtime.h"
/// @todo XXX REVIEW quick hack
static inline const struct s_plant * __boiler_to_plant(void * priv)
{
	return (runtime_get()->plant);
}

FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__boiler_to_plant, s_boiler_priv, valve_ret)

int hs_boiler_parse(struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,		"idle_mode",		false,	fcp_enum_s_boiler_priv_idle_mode,	NULL, },
		{ NODEFLT|NODEINT,	"hysteresis",		true,	fcp_temp_s_boiler_priv_hysteresis,	NULL, },
		{ NODEFLT|NODEINT,	"limit_thardmax",	true,	fcp_temp_s_boiler_priv_limit_thardmax,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmax",		false,	fcp_temp_s_boiler_priv_limit_tmax,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tmin",		false,	fcp_temp_s_boiler_priv_limit_tmin,	NULL, },
		{ NODEFLT|NODEINT,	"limit_treturnmin",	false,	fcp_temp_s_boiler_priv_limit_treturnmin, NULL, },
		{ NODEFLT|NODEINT,	"t_freeze",		true,	fcp_temp_s_boiler_priv_t_freeze,	NULL, },
		{ NODEINT|NODEDUR,	"burner_min_time",	false,	fcp_tk_s_boiler_priv_burner_min_time,	NULL, },
		{ NODESTR,		"tid_boiler",		true,	fcp_inputs_temperature_s_boiler_priv_tid_boiler,	NULL, },
		{ NODESTR,		"tid_boiler_return",	false,	fcp_inputs_temperature_s_boiler_priv_tid_boiler_return, NULL, },
		{ NODESTR,		"rid_burner_1",		true,	fcp_outputs_relay_s_boiler_priv_rid_burner_1,	NULL, },
		{ NODESTR,		"rid_burner_2",		false,	fcp_outputs_relay_s_boiler_priv_rid_burner_2,	NULL, },
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
