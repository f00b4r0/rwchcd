//
//  filecfg/parse/outputs_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global outputs system parsing implementation.
 *
\verbatim
 outputs {
	relays { ... };
 };
\endverbatim
 */

#include <stdlib.h>

#include "filecfg_parser.h"
#include "io/outputs/relay.h"
#include "relay_parse.h"
#include "io/outputs.h"
#include "outputs_parse.h"

extern struct s_outputs Outputs;


static int outputs_relay_wrap_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_outputs * const o = priv;
	struct s_relay * r;
	int ret;

	if (o->relays.last >= o->relays.n)
		return (-EOOM);

	if (-ENOTFOUND != outputs_relay_fbn(node->value.stringval))
		return (-EEXISTS);

	r = &o->relays.all[o->relays.last];

	ret = filecfg_relay_parse(r, node);
	if (ALL_OK == ret)
		o->relays.last++;

	return (ret);
}

static int outputs_relays_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_outputs * const outputs = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "relay");

	if (!n)
		return (-EEMPTY);

	if (n >= ORID_MAX)
		return (-ETOOBIG);

	outputs->relays.all = calloc(n, sizeof(outputs->relays.all[0]));
	if (!outputs->relays.all)
		return (-EOOM);

	outputs->relays.n = (orid_t)n;
	//outputs->relays.last = 0;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "relay", outputs_relay_wrap_parse));
}

int filecfg_outputs_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"relays",	false,	outputs_relays_parse,	NULL, },
	};
	//struct s_runtime * const runtime = priv;
	struct s_outputs * outputs = &Outputs;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	/* init outputs - clears data used by config */
	ret = outputs_init();
	if (ret) {
		pr_err(_("Failed to initialize outputs (%d)"), ret);
		return (ret);
	}

	ret = filecfg_parser_run_parsers(outputs, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = rwchcd_add_subsyscb("outputs", outputs_online, outputs_offline, outputs_exit);
	if (ALL_OK != ret)
		outputs_exit();

	return (ret);
}

int filecfg_outputs_parse_helper_rid(orid_t *rid, const struct s_filecfg_parser_node * const node)
{
	int ret;
	const char *str = node->value.stringval;

	assert(NODESTR == node->type);

	ret = outputs_relay_fbn(str);
	if (ret < 0)
		return (ret);

	*rid = (orid_t)ret;

	return (ALL_OK);
}
