//
//  filecfg/parse/pump_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration parsing.
 *
\verbatim
 pump "pump name" {
	 shared no;
	 cooldown_time 2mn;
	 rid_pump "rid name";
 };
\endverbatim
 */

#include <string.h>

#include "pump_parse.h"
#include "plant/pump_priv.h"
#include "plant/plant.h"
#include "filecfg_parser.h"
#include "outputs_parse.h"

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_pump, shared)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_pump, cooldown_time)
FILECFG_OUTPUTS_PARSER_RELAY_PARSE_SET_FUNC(s_pump, rid_pump)

int filecfg_pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"shared",		false,	fcp_bool_s_pump_shared,			NULL, },
		{ NODEINT|NODEDUR,	"cooldown_time",	false,	fcp_tk_s_pump_cooldown_time,		NULL, },
		{ NODESTR,		"rid_pump",		true,	fcp_outputs_relay_s_pump_rid_pump,	NULL, },
	};
	struct s_pump * restrict const pump = priv;
	int ret;

	// we receive a 'pump' node with a valid string attribute which is the pump name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(pump, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	pump->name = strdup(node->value.stringval);
	if (!pump->name)
		return (-EOOM);

	pump->set.configured = true;

	return (ALL_OK);
}
