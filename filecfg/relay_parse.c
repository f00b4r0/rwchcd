//
//  filecfg/relay_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global relay system parsing implementation.
 *
\verbatim
 relay "master pumps" {
	operation "all";
	missing "ignore";
	targets {
		target {
			backend "toto";
			name "pump1";
		};
		target {
			backend "titi";
			name "pump2";
		};
	};
 };
\endverbatim
 */

#include <stdlib.h>	// calloc
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "lib.h"
#include "filecfg_parser.h"
#include "relay.h"
#include "relay_parse.h"


// XXX ENSURE PARSING ORDER FOR targets

static const char * const relay_op_str[] = {
	[R_OP_FIRST]	= "first",
	[R_OP_ALL]	= "all",
};

static const char * const relay_miss_str[] = {
	[R_MISS_FAIL]	= "fail",
	[R_MISS_IGN]	= "ignore",
};

static int target_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_relay * const r = priv;
	int ret;

	if (r->rlast >= r->rnum)
		return (-EOOM);		// cannot happen

	ret = filecfg_parser_rid_parse(&r->rlist[r->rlast], node);
	if (ALL_OK != ret)
		return (ret);

	r->rlast++;

	return (ALL_OK);
}

static int targets_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_relay * const r = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "target");

	if (!n)
		return (-EEMPTY);	// XXX error message

	if (n >= UINT_FAST8_MAX)	// r->rnum is uint_fast8_t
		return (-ETOOBIG);

	r->rlist = calloc(n, sizeof(r->rlist[0]));
	if (!r->rlist)
		return (-EOOM);

	r->rnum = (uint_fast8_t)n;

	return (filecfg_parser_parse_listsiblings(priv, node->children, "target", target_parse));
}

FILECFG_PARSER_ENUM_PARSE_SET_FUNC(relay_op_str, s_relay, op)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(relay_miss_str, s_relay, missing)

/**
 * Parse an output relay from config.
 * @param priv an allocated relay structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
int filecfg_relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"operation",	false,	fcp_enum_s_relay_op,		NULL, },
		{ NODESTR,	"missing",	false,	fcp_enum_s_relay_missing,	NULL, },
		{ NODELST,	"targets",	true,	targets_parse,			NULL, },
	};
	struct s_relay * const r = priv;
	int ret;

	assert(node);
	assert(node->value.stringval);

	if (!r)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	r->name = strdup(node->value.stringval);
	if (!r->name)
		return (-EOOM);

	r->set.configured = true;

	return (ALL_OK);
}

