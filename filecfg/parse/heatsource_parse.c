//
//  filecfg/parse/heatsource_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource file configuration parsing.
 *
\verbatim
 heatsources {
	 heatsource "chaudière" {
		 log yes;
		 runmode "auto";
		 schedid "default";
		 type "type name" { ... };
		 consumer_sdelay 360;
	 };
 };
\endverbatim
 */

#include <string.h>

#include "heatsource_parse.h"
#include "filecfg_parser.h"
#include "boiler_parse.h"

#include "scheduler.h"
#include "plant/heatsource_priv.h"

static int heatsource_type_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_heatsource * const heatsource = priv;
	const char * str = node->value.stringval;
	int ret;

	if (!strcmp(str, "boiler"))
		ret = hs_boiler_parse(heatsource, node);
	else
		ret = -EUNKNOWN;

	return (ret);
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_heatsource, log)
FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(s_heatsource, runmode)
FILECFG_PARSER_PRIO_PARSE_SET_FUNC(s_heatsource, prio)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_heatsource, consumer_sdelay)
FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(s_heatsource, schedid)

int filecfg_heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTC,		"type",			true,	heatsource_type_parse,			NULL, },
		{ NODEBOL,		"log",			false,	fcp_bool_s_heatsource_log,		NULL, },
		{ NODESTR,		"runmode",		true,	fcp_runmode_s_heatsource_runmode,	NULL, },
		{ NODEINT,		"prio",			false,	fcp_prio_s_heatsource_prio,		NULL, },
		{ NODEINT|NODEDUR,	"consumer_sdelay",	false,	fcp_tk_s_heatsource_consumer_sdelay,	NULL, },
		{ NODESTR,		"schedid",		false,	fcp_schedid_s_heatsource_schedid,	NULL, },
	};
	struct s_heatsource * restrict const heatsource = priv;
	int ret;

	// we receive a 'heatsource' node with a valid string attribute which is the heatsource name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(heatsource, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	heatsource->name = strdup(node->value.stringval);
	if (!heatsource->name)
		return (-EOOM);

	heatsource->set.configured = true;

	return (ALL_OK);
}
