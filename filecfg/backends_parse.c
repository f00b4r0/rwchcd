//
//  filecfg/backends_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration parsing.
 */

#include <stdlib.h>

#include "hw_backends/dummy/filecfg.h"
#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif

#include "backends_parse.h"
#include "filecfg_parser.h"
#include "rwchcd.h"
#include "hw_backends.h"

typedef int (* const hw_bknd_parser_t)(const struct s_filecfg_parser_node * const);

extern struct s_hw_backends HW_backends;

static hw_bknd_parser_t HWparsers[] = {
	dummy_filecfg_parse,
#ifdef HAS_HWP1		// XXX
	hw_p1_filecfg_parse,
#endif
};

static int hardware_backend_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	unsigned int i;
	int ret = -EGENERIC;

	for (i = 0; ret && (i < ARRAY_SIZE(HWparsers)); i++)
		ret = HWparsers[i](node);

	return (ret);
}

int filecfg_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hw_backends * const b = &HW_backends;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "backend");

	if (!n)
		return (-EEMPTY);

	if (n >= UINT_FAST8_MAX)	// b->n is uint_fast8_t
		return (-ETOOBIG);

	b->all = calloc(n, sizeof(b->all[0]));
	if (!b->all)
		return (-EOOM);

	b->n = (bid_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
}
