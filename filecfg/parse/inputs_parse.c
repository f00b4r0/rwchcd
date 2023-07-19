//
//  filecfg/parse/inputs_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global inputs system parsing implementation.
 *
\verbatim
 inputs {
  	temperatures { ... };
  	switches { ... };
 };
\endverbatim
 */

#include <stdlib.h>

#include "filecfg_parser.h"
#include "io/inputs/temperature.h"
#include "temperature_parse.h"
#include "io/inputs/switch.h"
#include "switch_parse.h"
#include "io/inputs.h"
#include "inputs_parse.h"

extern struct s_inputs Inputs;

static int inputs_temperature_wrap_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_inputs * const i = priv;
	struct s_temperature * t;
	int ret;

	if (i->temps.last >= i->temps.n)
		return (-EOOM);

	if (-ENOTFOUND != inputs_temperature_fbn(node->value.stringval))
		return (-EEXISTS);

	t = &i->temps.all[i->temps.last];

	ret = filecfg_temperature_parse(t, node);
	if (ALL_OK == ret)
		i->temps.last++;

	return (ret);
}

static int inputs_switch_wrap_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_inputs * const i = priv;
	struct s_switch * s;
	int ret;

	if (i->switches.last >= i->switches.n)
		return (-EOOM);

	if (-ENOTFOUND != inputs_fbn(INPUT_SWITCH, node->value.stringval))
		return (-EEXISTS);

	s = &i->switches.all[i->switches.last];

	ret = filecfg_switch_parse(s, node);
	if (ALL_OK == ret)
		i->switches.last++;

	return (ret);
}

static int inputs_generic_parse(const enum e_input_type t, const char * name, void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	int (* wrap_parse)(void * restrict const priv, const struct s_filecfg_parser_node * const node);
	struct s_inputs * const inputs = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, name);

	if (!n)
		return (-EEMPTY);

	if (n >= INID_MAX)
		return (-ETOOBIG);

	switch (t) {
		case INPUT_TEMP:
			inputs->temps.all = calloc(n, sizeof(inputs->temps.all[0]));
			if (!inputs->temps.all)
				return (-EOOM);
			inputs->temps.n = (inid_t)n;
			wrap_parse = inputs_temperature_wrap_parse;
			break;
		case INPUT_SWITCH:
			inputs->switches.all = calloc(n, sizeof(inputs->switches.all[0]));
			if (!inputs->switches.all)
				return (-EOOM);
			inputs->switches.n = (inid_t)n;
			wrap_parse = inputs_switch_wrap_parse;
			break;
		case INPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (filecfg_parser_parse_namedsiblings(priv, node->children, name, wrap_parse));
}

static int inputs_temperatures_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (inputs_generic_parse(INPUT_TEMP, "temperature", priv, node));
}

static int inputs_switches_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (inputs_generic_parse(INPUT_SWITCH, "switch", priv, node));
}

int filecfg_inputs_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"temperatures",	false,	inputs_temperatures_parse,	NULL, },
		{ NODELST,	"switches",	false,	inputs_switches_parse,		NULL, },
	};
	//struct s_runtime * const runtime = priv;
	struct s_inputs * inputs = &Inputs;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	/* init inputs - clears data used by config */
	ret = inputs_init();
	if (ret) {
		pr_err(_("Failed to initialize inputs (%d)"), ret);
		return (ret);
	}

	ret = filecfg_parser_run_parsers(inputs, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = rwchcd_add_subsyscb("inputs", inputs_online, inputs_offline, inputs_exit);
	if (ALL_OK != ret)
		inputs_exit();

	return (ret);
}

int filecfg_inputs_parse_helper_inid(const enum e_input_type t, inid_t *inid, const struct s_filecfg_parser_node * const node)
{
	int ret;
	const char *str = node->value.stringval;

	assert(NODESTR == node->type);

	ret = inputs_fbn(t, str);
	if (ret < 0)
		return (ret);

	*inid = (inid_t)ret;

	return (ALL_OK);
}
