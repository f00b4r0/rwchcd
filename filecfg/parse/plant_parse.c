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

#include <stdlib.h>

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

static int plant_pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	struct s_pump * pump;
	int ret;

	if (plant->pumps.last >= plant->pumps.n)
		return (-EOOM);

	if (plant_fbn_pump(plant, node->value.stringval))
		return (-EEXISTS);

	pump = &plant->pumps.all[plant->pumps.last];
	ret = filecfg_pump_parse(pump, node);
	if (ALL_OK == ret)
		plant->pumps.last++;

	return (ret);
}

static int plant_pumps_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * const plant = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "pump");

	if (!n)
		return (-EEMPTY);

	if (n >= PLID_MAX)
		return (-ETOOBIG);

	plant->pumps.all = calloc(n, sizeof(plant->pumps.all[0]));
	if (!plant->pumps.all)
		return (-EOOM);

	plant->pumps.n = (plid_t)n;
	plant->pumps.last = 0;

	ret = filecfg_parser_parse_namedsiblings(plant, node->children, "pump", plant_pump_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all pumps (names)
	free(plant->pumps.all);
	return (ret);
}

static int plant_valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	struct s_valve * valve;
	int ret;

	if (plant->valves.last >= plant->valves.n)
		return (-EOOM);

	if (plant_fbn_valve(plant, node->value.stringval))
		return (-EEXISTS);

	valve = &plant->valves.all[plant->valves.last];
	ret = filecfg_valve_parse(valve, node);
	if (ALL_OK == ret)
		plant->valves.last++;

	return (ret);
}

static int plant_valves_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * const plant = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "valve");

	if (!n)
		return (-EEMPTY);

	if (n >= PLID_MAX)
		return (-ETOOBIG);

	plant->valves.all = calloc(n, sizeof(plant->valves.all[0]));
	if (!plant->valves.all)
		return (-EOOM);

	plant->valves.n = (plid_t)n;
	plant->valves.last = 0;

	ret = filecfg_parser_parse_namedsiblings(plant, node->children, "valve", plant_valve_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all valves (names)
	free(plant->valves.all);
	return (ret);
}

static int plant_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	struct s_hcircuit * hcircuit;
	int ret;

	if (plant->hcircuits.last >= plant->hcircuits.n)
		return (-EOOM);

	if (plant_fbn_hcircuit(plant, node->value.stringval))
		return (-EEXISTS);

	hcircuit = &plant->hcircuits.all[plant->hcircuits.last];

	// set plant data
	hcircuit->pdata = &plant->pdata;

	ret = filecfg_hcircuit_parse(hcircuit, node);
	if (ALL_OK == ret)
		plant->hcircuits.last++;

	return (ret);
}

static int plant_hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * const plant = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "hcircuit");

	if (!n)
		return (-EEMPTY);

	if (n >= PLID_MAX)
		return (-ETOOBIG);

	plant->hcircuits.all = calloc(n, sizeof(plant->hcircuits.all[0]));
	if (!plant->hcircuits.all)
		return (-EOOM);

	plant->hcircuits.n = (plid_t)n;
	plant->hcircuits.last = 0;

	ret = filecfg_parser_parse_namedsiblings(plant, node->children, "hcircuit", plant_hcircuit_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all hcircuits (names)
	free(plant->hcircuits.all);
	return (ret);
}

static int plant_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	struct s_dhwt * dhwt;
	int ret;

	if (plant->dhwts.last >= plant->dhwts.n)
		return (-EOOM);

	if (plant_fbn_dhwt(plant, node->value.stringval))
		return (-EEXISTS);

	dhwt = &plant->dhwts.all[plant->dhwts.last];

	// set plant data
	dhwt->pdata = &plant->pdata;

	ret = filecfg_dhwt_parse(dhwt, node);
	if (ALL_OK == ret)
		plant->dhwts.last++;

	return (ret);
}

static int plant_dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * const plant = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "dhwt");

	if (!n)
		return (-EEMPTY);

	if (n >= PLID_MAX)
		return (-ETOOBIG);

	plant->dhwts.all = calloc(n, sizeof(plant->dhwts.all[0]));
	if (!plant->dhwts.all)
		return (-EOOM);

	plant->dhwts.n = (plid_t)n;
	plant->dhwts.last = 0;

	ret = filecfg_parser_parse_namedsiblings(plant, node->children, "dhwt", plant_dhwt_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all dhwts (names)
	free(plant->dhwts.all);
	return (ret);
}

static int plant_heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * restrict const plant = priv;
	struct s_heatsource * heatsource;
	int ret;

	if (plant->heatsources.last >= plant->heatsources.n)
		return (-EOOM);

	if (plant_fbn_heatsource(plant, node->value.stringval))
		return (-EEXISTS);

	heatsource = &plant->heatsources.all[plant->heatsources.last];

	// set plant data
	heatsource->pdata = &plant->pdata;

	ret = filecfg_heatsource_parse(heatsource, node);
	if (ALL_OK == ret)
		plant->heatsources.last++;

	return (ret);
}

static int plant_heatsources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_plant * const plant = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "heatsource");

	if (!n)
		return (-EEMPTY);

	if (n >= PLID_MAX)
		return (-ETOOBIG);

	plant->heatsources.all = calloc(n, sizeof(plant->heatsources.all[0]));
	if (!plant->heatsources.all)
		return (-EOOM);

	plant->heatsources.n = (plid_t)n;
	plant->heatsources.last = 0;

	ret = filecfg_parser_parse_namedsiblings(plant, node->children, "heatsource", plant_heatsource_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all heatsources (names)
	free(plant->heatsources.all);
	return (ret);
}

int filecfg_plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"config",	false,	plant_config_parse,	NULL, },
		{ NODELST,	"pumps",	false,	plant_pumps_parse,	NULL, },
		{ NODELST,	"valves",	false,	plant_valves_parse,	NULL, },
		{ NODELST,	"dhwts",	false,	plant_dhwts_parse,	NULL, },
		{ NODELST,	"hcircuits",	false,	plant_hcircuits_parse,	NULL, },
		{ NODELST,	"heatsources",	false,	plant_heatsources_parse,NULL, },
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
