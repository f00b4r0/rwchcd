//
//  filecfg/parse/outputs_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global outputs system parsing API.
 */


#ifndef outputs_parse_h
#define outputs_parse_h

#include "filecfg_parser.h"
#include "io/outputs.h"

int filecfg_outputs_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_outputs_parse_helper_outid(const enum e_output_type t, outid_t *outid, const struct s_filecfg_parser_node * const node);

#define FILECFG_OUTPUTS_PARSER_RELAY_PARSE_SET_FUNC(_struct, _setmember)	\
static int fcp_outputs_relay_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_outputs_parse_helper_outid(OUTPUT_RELAY, &s->set._setmember, n));		\
}

#endif /* outputs_parse_h */
