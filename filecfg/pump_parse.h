//
//  filecfg/pump_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration parsing API.
 */

#ifndef pump_parse_h
#define pump_parse_h

#include "filecfg_parser.h"

int filecfg_pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* pump_parse_h */
