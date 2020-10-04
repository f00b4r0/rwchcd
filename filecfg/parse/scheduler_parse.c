//
//  filecfg/parse/scheduler_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler subsystem file configuration parsing.
 */

#include <string.h>	// strcmp/memcpy

#include "scheduler_parse.h"
#include "scheduler.h"
#include "filecfg_parser.h"

static int scheduler_fcp_entry_time_wday(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	int iv = node->value.intval;

	if ((iv < 0) || (iv > 7))
		return (-EINVALID);
	// convert Sunday if necessary
	if (7 == iv)
		iv = 0;
	schent->time.wday = iv;
	return (ALL_OK);
}

static int scheduler_fcp_entry_time_hour(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	int iv = node->value.intval;

	if ((iv < 0) || (iv > 23))
		return (-EINVALID);

	schent->time.hour = iv;
	return (ALL_OK);
}

static int scheduler_fcp_entry_time_min(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	int iv = node->value.intval;

	if ((iv < 0) || (iv > 59))
		return (-EINVALID);

	schent->time.min = iv;
	return (ALL_OK);
}

/**
 * Scheduler entry time parse.
 * @param priv a struct s_schedule_e
 * @param node a "time" node
 * @todo wishlist: parse a single entry spanning multiple weekdays.
 */
static int scheduler_entry_time_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT,	"wday",	true,	scheduler_fcp_entry_time_wday,	NULL, },
		{ NODEINT,	"hour",	true,	scheduler_fcp_entry_time_hour,	NULL, },
		{ NODEINT,	"min",	true,	scheduler_fcp_entry_time_min,	NULL, },
	};
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}

static int scheduler_fcp_entry_param_legionella(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	schent->params.legionella = node->value.boolval;
	return (ALL_OK);
}

static int scheduler_fcp_entry_param_recycle(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	schent->params.recycle = node->value.boolval;
	return (ALL_OK);
}

static int scheduler_fcp_entry_param_runmode(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	return (filecfg_parser_runmode_parse(&schent->params.runmode, node));
}

static int scheduler_fcp_entry_param_dhwmode(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedule_e * restrict const schent = priv;
	return (filecfg_parser_runmode_parse(&schent->params.dhwmode, node));
}

/**
* Scheduler entry params parse.
* @param priv a struct s_schedule_e
* @param node a "params" node
*/
static int scheduler_entry_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"runmode",	false,	scheduler_fcp_entry_param_runmode,	NULL, },
		{ NODESTR,	"dhwmode",	false,	scheduler_fcp_entry_param_dhwmode,	NULL, },
		{ NODEBOL,	"legionella",	false,	scheduler_fcp_entry_param_legionella,	NULL, },
		{ NODEBOL,	"recycle",	false,	scheduler_fcp_entry_param_recycle,	NULL, },
	};
	struct s_schedule_e * const schent = priv;
	int ret;

	// we receive an 'entry' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// reset buffer and set mode defaults
	memset(&schent->params, 0, sizeof(schent->params));
	schent->params.runmode = RM_UNKNOWN;
	schent->params.dhwmode = RM_UNKNOWN;

	return (filecfg_parser_run_parsers(schent, parsers, ARRAY_SIZE(parsers)));
}

static int scheduler_entry_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"time",		true,	scheduler_entry_time_parse,	NULL, },
		{ NODELST,	"params",	true,	scheduler_entry_params_parse,	NULL, },
	};
	const schedid_t schedid = *(schedid_t *)priv;
	struct s_schedule_e schent;
	int ret;

	// we receive an 'entry' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&schent, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = scheduler_add_entry(schedid, &schent);
	switch (ret) {
		case -EEXISTS:
			filecfg_parser_pr_err(_("Line %d: a schedule entry with the same time is already configured"), node->lineno);
			break;
		default:
			break;
	}

	return (ret);
}

static int scheduler_schedule_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	int schedid;

	if (!node)
		return (-EINVALID);

	if (!node->children)
		return (-EEMPTY);	// we only accept NODESTR backend node with children

	if (strlen(node->value.stringval) <= 0)
		return (-EINVALID);

	schedid = scheduler_add_schedule(node->value.stringval);
	if (schedid <= 0) {
		switch (schedid) {
			case -EEXISTS:
				filecfg_parser_pr_err(_("Line %d: a schedule with the same name (\'%s\') is already configured"), node->lineno, node->value.stringval);
				break;
			default:
				break;
		}
		return (schedid);
	}

	return (filecfg_parser_parse_listsiblings(&schedid, node->children, "entry", scheduler_entry_parse));
}

/**
 * Parse scheduler configuration.
 * @param priv unused
 * @param node a `scheduler` node
 * @return exec status
 */
int filecfg_scheduler_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "schedule", scheduler_schedule_parse));
}

