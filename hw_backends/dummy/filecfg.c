//
//  hw_backends/dummy/filecfg.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Dummy backend file configuration implementation.
\verbatim
backend "toto" {
	type "dummy";
	temperatures {
 		temperature "test1" {
 			value 20.0;
 		};
 		...
 	};
 	relays {
 		relay "out";
 		...
 	};
 };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "filecfg.h"
#include "filecfg_parser.h"

FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_dummy_temperature, value)

static int temperature_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"value",	true,	fcp_temp_s_dummy_temperature_value,	NULL, },
	};
	struct s_dummy_temperature * t = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(t, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	t->name = strdup(node->value.stringval);
	t->set.configured = true;

	return (ALL_OK);
}
static int temperature_wrap_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dummy_pdata * const hw = priv;
	struct s_dummy_temperature * t;
	int ret;

	if (hw->in.temps.l >= hw->in.temps.n)
		return (-EOOM);

	if (-ENOTFOUND != dummy_input_ibn(hw, HW_INPUT_TEMP, node->value.stringval))
		return (-EEXISTS);

	t = &hw->in.temps.all[hw->in.temps.l];

	ret = temperature_parse(t, node);
	if (ALL_OK == ret)
		hw->in.temps.l++;

	return (ret);
}

static int temperatures_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dummy_pdata * const hw = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "temperature");

	if (!n)
		return (-EINVALID);

	if (n >= UINT_FAST8_MAX)
		return (-ETOOBIG);

	hw->in.temps.all = calloc(n, sizeof(hw->in.temps.all[0]));
	if (!hw->in.temps.all)
		return (-EOOM);

	hw->in.temps.n = (uint_fast8_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "temperature", temperature_wrap_parse));
}

static int relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dummy_relay * r = priv;

	r->name = strdup(node->value.stringval);
	r->set.configured = true;

	return (ALL_OK);
}

static int relay_wrap_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dummy_pdata * const hw = priv;
	struct s_dummy_relay * r;
	int ret;

	if (hw->out.rels.l >= hw->out.rels.n)
		return (-EOOM);

	if (-ENOTFOUND != dummy_output_ibn(hw, HW_OUTPUT_RELAY, node->value.stringval))
		return (-EEXISTS);

	r = &hw->out.rels.all[hw->out.rels.l];

	ret = relay_parse(r, node);
	if (ALL_OK == ret)
		hw->out.rels.l++;

	return (ret);
}

static int relays_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_dummy_pdata * const hw = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "relay");

	if (!n)
		return (-EINVALID);

	if (n >= UINT_FAST8_MAX)
		return (-ETOOBIG);

	hw->out.rels.all = calloc(n, sizeof(hw->out.rels.all[0]));
	if (!hw->out.rels.all)
		return (-EOOM);

	hw->out.rels.n = (uint_fast8_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "relay", relay_wrap_parse));
}

/**
 * Parse dummy backend configuration.
 * @param node 'backend' node to process data from
 * @return exec status
 */
int dummy_filecfg_parse(const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"type",		true,	NULL,			NULL, },
		{ NODELST,	"temperatures",	false,	temperatures_parse,	NULL, },
		{ NODELST,	"relays",	false,	relays_parse,		NULL, },
	};
	struct s_dummy_pdata * hw;
	int ret;

	if (!node)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	if (strcmp("dummy", parsers[0].node->value.stringval))	// wrong type - XXX REVIEW DIRECT INDEXING
		return (-ENOTFOUND);

	// we have the right type, let's go ahead
	dbgmsg(1, 1, "Dummy: config found");

	// instantiate dummy hw
	hw = calloc(1, sizeof(*hw));
	if (!hw)
		return (-EOOM);

	// parse node list in specified order
	ret = filecfg_parser_run_parsers(hw, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		// XXX cleanup
		filecfg_parser_pr_err(_("Dummy: config parse error"));
		return (ret);
	}

	// register hardware backend
	ret = dummy_backend_register(hw, node->value.stringval);
	if (ret < 0) {
		// XXX cleanup
		filecfg_parser_pr_err(_("Dummy: backend registration failed for %s (%d)"), node->value.stringval, ret);
	}

	return (ret);
}
