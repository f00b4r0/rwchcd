//
//  filecfg/pump_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration parsing.
 */

#include "pump_parse.h"
#include "pump.h"
#include "plant.h"
#include "filecfg_parser.h"

FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_pump, cooldown_time)
FILECFG_PARSER_RID_PARSE_SET_FUNC(s_pump, rid_pump)

int filecfg_pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
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