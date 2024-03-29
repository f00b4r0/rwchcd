//
//  filecfg/parse/dhwt_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT file configuration parsing.
 *
\verbatim
 dhwt "dhwt name" {
	 log yes;
 	 electric_hasthermostat no;
  	 anti_legionella yes;
  	 legionella_recycle no;
  	 electric_recycle yes;
    	 prio 0;
	 runmode "auto";
	 schedid "default";
 *	 electric_schedid "";
	 dhwt_cprio "paralmax";
	 force_mode "never";
	 tid_bottom "boiler";
 	 tid_top "";
 	 tid_win "";
	 sid_selfheatershed "";
 	 rid_selfheater "";
	 params { ... };
 	 pump_feed "";
 	 pump_dhwrecycle "";
 	 valve_feedisol "";
 };
\endverbatim
 */

#include <string.h>	// strlen/strcmp

#include "outputs_parse.h"
#include "inputs_parse.h"
#include "dhwt_parse.h"
#include "filecfg_parser.h"
#include "plant/dhwt_priv.h"

#include "scheduler.h"
#include "plant/plant.h"
#include "plant/pump.h"


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

int filecfg_dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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

	filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}

#include "plant/plant_priv.h"
static inline const struct s_plant * __dhwt_to_plant(void * priv)
{
	return (pdata_to_plant(((struct s_dhwt *)priv)->pdata));
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, log)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, electric_hasthermostat)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, anti_legionella)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, legionella_recycle)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_dhwt, electric_recycle)
FILECFG_PARSER_PRIO_PARSE_SET_FUNC(s_dhwt, prio)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_dhwt, runmode)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_dhwt, tid_bottom)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_dhwt, tid_top)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_dhwt, tid_win)
FILECFG_INPUTS_PARSER_SWITCH_PARSE_SET_FUNC(s_dhwt, sid_selfheatershed)
FILECFG_OUTPUTS_PARSER_RELAY_PARSE_SET_FUNC(s_dhwt, rid_selfheater)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_dhwt, schedid)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_dhwt, electric_schedid)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, false, s_dhwt, tthresh_dhwisol)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, pump_feed)
FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, pump_dhwrecycle)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, valve_feedisol)
FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(__dhwt_to_plant, s_dhwt, valve_dhwisol)

static const char * dhwt_cprio_str[] = {
	[DHWTP_PARALMAX]	= "paralmax",
	[DHWTP_PARALDHW]	= "paraldhw",
	[DHWTP_SLIDMAX]		= "slidmax",
	[DHWTP_SLIDDHW]		= "sliddhw",
	[DHWTP_ABSOLUTE]	= "absolute",
};

FILECFG_PARSER_ENUM_PARSE_SET_FUNC(dhwt_cprio_str, s_dhwt, dhwt_cprio)

static const char * dhwt_force_mode_str[] = {
	[DHWTF_NEVER]		= "never",
	[DHWTF_FIRST]		= "first",
	[DHWTF_ALWAYS]		= "always",
};

FILECFG_PARSER_ENUM_PARSE_SET_FUNC(dhwt_force_mode_str, s_dhwt, force_mode)

static int fcp_dhwt_params(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	return (filecfg_dhwt_params_parse(&dhwt->set.params, node));
}

int filecfg_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,	"log",			false,	fcp_bool_s_dhwt_log,			NULL, },
		{ NODEBOL,	"electric_hasthermostat",	false,	fcp_bool_s_dhwt_electric_hasthermostat,	NULL, },
		{ NODEBOL,	"anti_legionella",	false,	fcp_bool_s_dhwt_anti_legionella,	NULL, },
		{ NODEBOL,	"legionella_recycle",	false,	fcp_bool_s_dhwt_legionella_recycle,	NULL, },
		{ NODEBOL,	"electric_recycle",	false,	fcp_bool_s_dhwt_electric_recycle,	NULL, },
		{ NODEINT,	"prio",			false,	fcp_prio_s_dhwt_prio,			NULL, },
		{ NODESTR,	"schedid",		false,	fcp_schedid_s_dhwt_schedid,		NULL, },
		{ NODESTR,	"electric_schedid",	false,	fcp_schedid_s_dhwt_electric_schedid,	NULL, },
		{ NODESTR,	"runmode",		true,	fcp_runmode_s_dhwt_runmode,		NULL, },
		{ NODESTR,	"dhwt_cprio",		false,	fcp_enum_s_dhwt_dhwt_cprio,		NULL, },
		{ NODESTR,	"force_mode",		false,	fcp_enum_s_dhwt_force_mode,		NULL, },
		{ NODESTR,	"tid_bottom",		false,	fcp_inputs_temperature_s_dhwt_tid_bottom,NULL, },
		{ NODESTR,	"tid_top",		false,	fcp_inputs_temperature_s_dhwt_tid_top,	NULL, },
		{ NODESTR,	"tid_win",		false,	fcp_inputs_temperature_s_dhwt_tid_win,	NULL, },
		{ NODESTR,	"sid_selfheatershed",	false,	fcp_inputs_switch_s_dhwt_sid_selfheatershed,	NULL, },
		{ NODESTR,	"rid_selfheater",	false,	fcp_outputs_relay_s_dhwt_rid_selfheater,NULL, },
		{ NODEFLT|NODEINT, "tthresh_dhwisol",	false,	fcp_temp_s_dhwt_tthresh_dhwisol,	NULL, },
		{ NODELST,	"params",		false,	fcp_dhwt_params,			NULL, },
		{ NODESTR,	"pump_feed",		false,	fcp_pump_s_dhwt_ppump_feed,		NULL, },
		{ NODESTR,	"pump_dhwrecycle",	false,	fcp_pump_s_dhwt_ppump_dhwrecycle,	NULL, },
		{ NODESTR,	"valve_feedisol",	false,	fcp_valve_s_dhwt_pvalve_feedisol,	NULL, },
		{ NODESTR,	"valve_dhwisol",	false,	fcp_valve_s_dhwt_pvalve_dhwisol,	NULL, },
	};
	struct s_dhwt * restrict const dhwt = priv;
	int ret;

	// we receive a 'dhwt' node with a valid string attribute which is the dhwt name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(dhwt, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	dhwt->name = strdup(node->value.stringval);
	if (!dhwt->name)
		return (-EOOM);

	dhwt->set.configured = true;

	return (ALL_OK);
}
