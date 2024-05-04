//
//  filecfg/parse/scheduler_parse.c
//  rwchcd
//
//  (C) 2020,2023-2024 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler subsystem file configuration parsing.
 *
\verbatim
 scheduler {
	 schedule "default" {
		 entry {
			 time { wday 0; hour 7; min 0; };	// wday can be a single digit, a quoted range "B-E" (B first day E last day) or quoted "all" for the entire week.
			 params { runmode "comfort"; dhwmode "comfort"; };
		 };
		 ...
 	 };
 };
\endverbatim
 */

#include <string.h>	// strcmp/memcpy
#include <stdlib.h>	// calloc

#include "scheduler_parse.h"
#include "scheduler.h"
#include "filecfg_parser.h"

/** Contiguous 8-bit mask ranging from l to h  */
#define GEN8MASK(l, h)	(uint8_t)(((~0U) - (1U << (l)) + 1) & (~0U >> (31 - (h))))

extern struct s_schedules Schedules;

struct s_schent_wrap {
	struct s_schedule_e schent;
	uint8_t bitdays;
};

static int scheduler_fcp_entry_time_wday(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schent_wrap * restrict const swrap = priv;
	const char * restrict const sv = node->value.stringval;
	int iv = node->value.intval, b, e;

	if (NODESTR == node->type) {
		if (strlen(sv) != 3)
			return (-EINVALID);

		// accept "B-E" range of days
		if ('-' == sv[1]) {
			b = sv[0] - '0';
			e = sv[2] - '0';

			// sanity check
			if ((b < 0) || (e < 0) || (b > 7) || (e > 7))
				return (-EINVALID);

			// convert Sunday
			if (7 == b)
				b = 0;
			if (7 == e)
				e = 0;

			swrap->bitdays = (b <= e) ? GEN8MASK(b, e) : (uint8_t)(GEN8MASK(b, 6) | GEN8MASK(0, e));
		}
		// accept "all" catchall to set all days
		else if (!strcmp("all", sv))
			swrap->bitdays = 0x7F;	// bits 0-6 - swrap->schent.time.wday is irrelevant
		else
			return (-EINVALID);
	}
	else {
		if ((iv < 0) || (iv > 7))
			return (-EINVALID);
		// convert Sunday if necessary
		if (7 == iv)
			iv = 0;
		swrap->bitdays = 0;
		swrap->schent.time.wday = iv;
	}
	return (ALL_OK);
}

static int scheduler_fcp_entry_time_hour(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schent_wrap * restrict const swrap = priv;
	int iv = node->value.intval;

	if ((iv < 0) || (iv > 23))
		return (-EINVALID);

	swrap->schent.time.hour = iv;
	return (ALL_OK);
}

static int scheduler_fcp_entry_time_min(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schent_wrap * restrict const swrap = priv;
	int iv = node->value.intval;

	if ((iv < 0) || (iv > 59))
		return (-EINVALID);

	swrap->schent.time.min = iv;
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
		{ NODEINT|NODESTR,"wday",true,	scheduler_fcp_entry_time_wday,	NULL, },
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
	struct s_schent_wrap * const swrap = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// reset buffer and set mode defaults
	memset(&swrap->schent.params, 0, sizeof(swrap->schent.params));
	swrap->schent.params.runmode = RM_UNKNOWN;
	swrap->schent.params.dhwmode = RM_UNKNOWN;

	return (filecfg_parser_run_parsers(&swrap->schent, parsers, ARRAY_SIZE(parsers)));
}

static int scheduler_entry_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"time",		true,	scheduler_entry_time_parse,	NULL, },
		{ NODELST,	"params",	true,	scheduler_entry_params_parse,	NULL, },
	};
	struct s_schedule * const sched = priv;
	struct s_schent_wrap swrap;
	unsigned int d;
	int ret;

	// we receive an 'entry' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&swrap, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	if (swrap.bitdays) {
		for (d = 0; d < 7; d++) {
			if (testbit(swrap.bitdays, d)) {
				swrap.schent.time.wday = (int)d;
				ret = scheduler_add_entry(sched, &swrap.schent);
				if (ALL_OK != ret)
					goto fail;
			}
		}
	}
	else
		ret = scheduler_add_entry(sched, &swrap.schent);

fail:
	switch (ret) {
		case -EEXISTS:
			filecfg_parser_pr_err(_("Line %d: a schedule entry covering the same time is already configured"), node->lineno);
			break;
		default:
			break;
	}

	return (ret);
}

static int scheduler_schedule_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_schedules * const scheds = priv;
	struct s_schedule * s;
	char * name;
	int ret;

	if (!node->children)
		return (-EEMPTY);

	if (strlen(node->value.stringval) <= 0)
		return (-EINVALID);

	if (scheds->lastid >= scheds->n)
		return (-EOOM);

	if (-ENOTFOUND != scheduler_schedid_by_name(node->value.stringval)) {
		filecfg_parser_pr_err(_("Line %d: a schedule with the same name (\'%s\') is already configured"), node->lineno, node->value.stringval);
		return (-EEXISTS);
	}

	name = strdup(node->value.stringval);
	if (!name)
		return (-EOOM);

	s = &scheds->all[scheds->lastid];

	ret = filecfg_parser_parse_listsiblings(s, node->children, "entry", scheduler_entry_parse);
	if (ALL_OK != ret)
		goto fail;

	s->name = name;
	scheds->lastid++;

	return (ALL_OK);

fail:
	free (name);
	return (ret);
}

/**
 * Parse scheduler configuration.
 * @param priv unused
 * @param node a `scheduler` node
 * @return exec status
 */
int filecfg_scheduler_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_schedules * schedules = &Schedules;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "schedule");

	if (!n)
		return (-EEMPTY);

	if (n >= SCHEDID_MAX)
		return (-ETOOBIG);

	schedules->all = calloc(n, sizeof(schedules->all[0]));
	if (!schedules->all)
		return (-EOOM);

	schedules->n = (schedid_t)n;
	schedules->lastid = 0;

	ret = filecfg_parser_parse_namedsiblings(schedules, node->children, "schedule", scheduler_schedule_parse);
	if (ALL_OK != ret)
		goto cleanup;

	// depends on nothing

	ret = rwchcd_add_subsyscb("scheduler", NULL, NULL, scheduler_exit);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	scheduler_exit();
	return (ret);
}

