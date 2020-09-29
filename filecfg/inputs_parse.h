//
//  filecfg/inputs_parse.h
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

int filecfg_inputs_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* inputs_parse_h */
