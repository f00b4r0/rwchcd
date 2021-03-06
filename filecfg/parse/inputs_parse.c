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
 };
\endverbatim
 */

#include <stdlib.h>

#include "filecfg_parser.h"
#include "io/inputs/temperature.h"
#include "temperature_parse.h"
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

static int inputs_temperatures_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_inputs * const inputs = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "temperature");

	if (!n)
		return (-EEMPTY);

	if (n >= ITID_MAX)
		return (-ETOOBIG);

	inputs->temps.all = calloc(n, sizeof(inputs->temps.all[0]));
	if (!inputs->temps.all)
		return (-EOOM);

	inputs->temps.n = (itid_t)n;
	//inputs->temps.last = 0;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "temperature", inputs_temperature_wrap_parse));
}

int filecfg_inputs_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"temperatures",	false,	inputs_temperatures_parse,	NULL, },
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

int filecfg_inputs_parse_helper_tid(itid_t *tid, const struct s_filecfg_parser_node * const node)
{
	int ret;
	const char *str = node->value.stringval;

	assert(NODESTR == node->type);

	ret = inputs_temperature_fbn(str);
	if (ret < 0)
		return (ret);

	*tid = (itid_t)ret;

	return (ALL_OK);
}
