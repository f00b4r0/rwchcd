//
//  filecfg/parse/boiler_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler heatsource file configuration parsing API.
 */

#ifndef boiler_parse_h
#define boiler_parse_h

#include "filecfg_parser.h"
#include "heatsource.h"

int hs_boiler_parse(struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node);

#endif /* boiler_parse_h */
