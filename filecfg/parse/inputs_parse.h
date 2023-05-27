//
//  filecfg/parse/inputs_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global inputs system parsing API.
 */

/*
 inputs {
	temperatures {
		temperature "name" {};
		...
	};
	...
 };
 */

#ifndef inputs_parse_h
#define inputs_parse_h

#include "filecfg_parser.h"
#include "io/inputs.h"

int filecfg_inputs_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_inputs_parse_helper_inid(const enum e_input_type t, inid_t *inid, const struct s_filecfg_parser_node * const node);

#define FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(_struct, _setmember)	\
static int fcp_inputs_temperature_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_inputs_parse_helper_inid(INPUT_TEMP, &s->set._setmember, n));		\
}

#endif /* inputs_parse_h */
