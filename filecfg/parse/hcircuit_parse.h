//
//  filecfg/hcircuit_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit file configuration parsing API.
 */

#ifndef hcircuit_parse_h
#define hcircuit_parse_h

#include "filecfg_parser.h"

int filecfg_hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* hcircuit_parse_h */
