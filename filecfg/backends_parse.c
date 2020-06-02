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

#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif

#include "backends_parse.h"
#include "filecfg_parser.h"
#include "rwchcd.h"

static int hardware_backend_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	int ret = ALL_OK;

#ifdef HAS_HWP1		// XXX
	ret = hw_p1_filecfg_parse(node);
#endif

	return (ret);
}

int filecfg_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
}
