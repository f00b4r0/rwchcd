//
//  filecfg/parse/switch_parse.c
//  rwchcd
//
//  (C) 2023 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global switch system parsing implementation.
 *
\verbatim
 switch "myswitch" {
	period 10s;
	ignstate on;
	operation "and";
	missing "ignoredef";
	sources {
		source {
			backend "toto";
			name "myswitch 1";
		};
		source {
			backend "titi";
			name "myswitch 2";
		};
		...
	};
 };
\endverbatim
 * `source` are name of backend and name of switch input within that backend.
 */

#include <stdlib.h>	// calloc
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "lib.h"
#include "hw_backends/hw_backends.h"
#include "backends_parse.h"
#include "io/inputs/switch.h"
#include "switch_parse.h"

#define filecfg_backends_sid_parse(priv, node)	filecfg_backends_parser_inid_parse(HW_INPUT_SWITCH, priv, node)

static const char * const switch_op_str[] = {
	[S_OP_FIRST]	= "first",
	[S_OP_AND]	= "and",
	[S_OP_OR]	= "or",
};

static const char * const switch_miss_str[] = {
	[S_MISS_FAIL]	= "fail",
	[S_MISS_IGN]	= "ignore",
	[S_MISS_IGNDEF]	= "ignoredef",
};

static int source_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_switch * const s = priv;
	int ret;

	if (s->slast >= s->snum)
		return (-EOOM);		// cannot happen

	ret = filecfg_backends_sid_parse(&s->slist[s->slast], node);
	if (ALL_OK != ret)
		return (ret);

	s->slast++;

	return (ALL_OK);
}

static int sources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_switch * const s = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "source");

	if (!n)
		return (-EEMPTY);	// XXX error message

	if (n >= UINT_FAST8_MAX)	// t->tnum is uint_fast8_t
		return (-ETOOBIG);

	s->slist = calloc(n, sizeof(s->slist[0]));
	if (!s->slist)
		return (-EOOM);

	s->snum = (uint_fast8_t)n;

	return (filecfg_parser_parse_listsiblings(priv, node->children, "source", source_parse));
}

FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_switch, period)
FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_switch, ignstate)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(switch_op_str, s_switch, op)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(switch_miss_str, s_switch, missing)

/**
 * Parse an input switch from config.
 * @param priv an allocated switch structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
int filecfg_switch_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEDUR,	"period",	true,	fcp_tk_s_switch_period,		NULL, },
		{ NODEBOL,	"ignstate",	false,	fcp_bool_s_switch_ignstate,	NULL, },
		{ NODESTR,	"op",		false,	fcp_enum_s_switch_op,		NULL, },
		{ NODESTR,	"missing",	false,	fcp_enum_s_switch_missing,	NULL, },
		{ NODELST,	"sources",	true,	sources_parse,			NULL, },
	};
	struct s_switch * const s = priv;
	int ret;

	assert(node);
	assert(node->value.stringval);

	if (!s)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// consistency checks
	if ((S_MISS_IGNDEF == s->set.missing)) {
		if (!parsers[1].node)
			filecfg_parser_pr_err("Invalid configuration: \"ignoredef\" set but no \"ignstate\" set!");
		return (-EINVALID);
	}

	s->name = strdup(node->value.stringval);
	if (!s->name)
		return (-EOOM);

	// force fetch at first run
	aser(&s->run.last_update, TIMEKEEP_MAX/2);

	s->set.configured = true;

	return (ALL_OK);
}

