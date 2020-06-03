//
//  filecfg/dhwt_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT file configuration parsing.
 */

#include <string.h>	// strlen/strcmp

#include "dhwt_parse.h"
#include "filecfg_parser.h"
#include "dhwt.h"

#include "scheduler.h"
#include "plant.h"


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

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
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
	const char * str = node->value.stringval;

	if	(!strcmp(str, "paralmax"))
		dhwt->set.dhwt_cprio = DHWTP_PARALMAX;
	else if (!strcmp(str, "paraldhw"))
		dhwt->set.dhwt_cprio = DHWTP_PARALDHW;
	else if (!strcmp(str, "slidmax"))
		dhwt->set.dhwt_cprio = DHWTP_SLIDMAX;
	else if (!strcmp(str, "sliddhw"))
		dhwt->set.dhwt_cprio = DHWTP_SLIDDHW;
	else if (!strcmp(str, "absolute"))
		dhwt->set.dhwt_cprio = DHWTP_ABSOLUTE;
	else
		return (-EINVALID);

	return (ALL_OK);
}

static int fcp_dhwt_force_mode(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	const char * str = node->value.stringval;

	if	(!strcmp(str, "never"))
		dhwt->set.force_mode = DHWTF_NEVER;
	else if (!strcmp(str, "first"))
		dhwt->set.force_mode = DHWTF_FIRST;
	else if (!strcmp(str, "always"))
		dhwt->set.force_mode = DHWTF_ALWAYS;
	else
		return (-EINVALID);

	return (ALL_OK);
}

static int fcp_dhwt_params(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dhwt * restrict const dhwt = priv;
	return (filecfg_dhwt_params_parse(&dhwt->set.params, node));
}

int filecfg_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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
