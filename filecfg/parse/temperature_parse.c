//
//  filecfg/temperature_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global temperature system parsing implementation.
 *
\verbatim
 temperature "outdoor" {
	period 10s;
	igntemp 20;
	operation "min";
	missing "ignoredef";
	sources {
		source {
			backend "toto";
			name "outdoor north";
		};
		source {
			backend "titi";
			name "outdoor south";
		};
	};
 };
\endverbatim
 * `source` are name of backend and name of temperature input within that backend.
 */

#include <stdlib.h>	// calloc
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "lib.h"
#include "hw_backends.h"
#include "backends_parse.h"
#include "temperature.h"
#include "temperature_parse.h"

#define filecfg_backends_tid_parse(priv, node)	filecfg_backends_parser_inid_parse(HW_INPUT_TEMP, priv, node)

static const char * const temp_op_str[] = {
	[T_OP_FIRST]	= "first",
	[T_OP_MIN]	= "min",
	[T_OP_MAX]	= "max",
};

static const char * const temp_miss_str[] = {
	[T_MISS_FAIL]	= "fail",
	[T_MISS_IGN]	= "ignore",
	[T_MISS_IGNDEF]	= "ignoredef",
};

static int source_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_temperature * const t = priv;
	int ret;

	if (t->tlast >= t->tnum)
		return (-EOOM);		// cannot happen

	ret = filecfg_backends_tid_parse(&t->tlist[t->tlast], node);
	if (ALL_OK != ret)
		return (ret);

	t->tlast++;

	return (ALL_OK);
}

static int sources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_temperature * const t = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "source");

	if (!n)
		return (-EEMPTY);	// XXX error message

	if (n >= UINT_FAST8_MAX)	// t->tnum is uint_fast8_t
		return (-ETOOBIG);

	t->tlist = calloc(n, sizeof(t->tlist[0]));
	if (!t->tlist)
		return (-EOOM);

	t->tnum = (uint_fast8_t)n;

	return (filecfg_parser_parse_listsiblings(priv, node->children, "source", source_parse));
}

FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_temperature, period)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_temperature, igntemp)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(temp_op_str, s_temperature, op)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(temp_miss_str, s_temperature, missing)

/**
 * Parse an input temperature from config.
 * @param priv an allocated temperature structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
int filecfg_temperature_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEDUR,	"period",	true,	fcp_tk_s_temperature_period,	NULL, },
		{ NODEINT|NODEFLT, "igntemp",	false,	fcp_temp_s_temperature_igntemp,	NULL, },
		{ NODESTR,	"operation",	false,	fcp_enum_s_temperature_op,	NULL, },
		{ NODESTR,	"missing",	false,	fcp_enum_s_temperature_missing,	NULL, },
		{ NODELST,	"sources",	true,	sources_parse,			NULL, },
	};
	struct s_temperature * const t = priv;
	int ret;

	assert(node);
	assert(node->value.stringval);

	if (!t)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// consistency checks
	if ((T_MISS_IGNDEF == t->set.missing)) {
		if (!t->set.igntemp)
			filecfg_parser_pr_err("Invalid configuration: \"ignoredef\" set but no \"igntemp\" set!");
		else if (!validate_temp(t->set.igntemp))
			filecfg_parser_pr_err("Invalid configuration: \"igntemp\" out of range!");
		return (-EINVALID);
	}

	t->name = strdup(node->value.stringval);
	if (!t->name)
		return (-EOOM);

	// force fetch at first run
	atomic_store_explicit(&t->run.last_update, TIMEKEEP_MAX/2, memory_order_relaxed);

	t->set.configured = true;

	return (ALL_OK);
}

