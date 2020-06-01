//
//  filecfg/valve_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve subsystem file configuration parsing API.
 */

#ifndef valve_parse_h
#define valve_parse_h

#include "filecfg_parser.h"

int filecfg_valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* valve_parse_h */
