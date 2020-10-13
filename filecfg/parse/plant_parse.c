//
//  filecfg/parse/plant_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant file configuration parsing.
 */

#include "plant_parse.h"
#include "filecfg_parser.h"
#include "plant/plant.h"
#include "pump_parse.h"
#include "valve_parse.h"
#include "dhwt_parse.h"
#include "hcircuit_parse.h"
#include "heatsource_parse.h"

#include "runtime.h"

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_plant, summer_maintenance)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, sleeping_delay)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, summer_run_interval)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_plant, summer_run_duration)

static int defconfig_def_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	return (filecfg_hcircuit_params_parse(&plant->pdata.set.def_hcircuit, node));
}

static int defconfig_def_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	return (filecfg_dhwt_params_parse(&plant->pdata.set.def_dhwt, node));
}

static int plant_config_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"summer_maintenance",	false,	fcp_bool_s_plant_summer_maintenance,	NULL, },
		{ NODEINT|NODEDUR,	"sleeping_delay",	false,	fcp_tk_s_plant_sleeping_delay,		NULL, },
		{ NODEINT|NODEDUR,	"summer_run_interval",	false,	fcp_tk_s_plant_summer_run_interval,	NULL, },
		{ NODEINT|NODEDUR,	"summer_run_duration",	false,	fcp_tk_s_plant_summer_run_duration,	NULL, },
		// the next two nodes affect plant->pdata
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
		if (!plant->set.summer_run_interval || !plant->set.summer_run_duration) {
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

static int dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "dhwt", filecfg_dhwt_parse));
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", filecfg_hcircuit_parse));
}

static int heatsources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "heatsource", filecfg_heatsource_parse));
}

int filecfg_plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
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