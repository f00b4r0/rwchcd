//
//  filecfg/parse/heatsource_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource file configuration parsing API.
 */

#ifndef heatsource_parse_h
#define heatsource_parse_h

#include "filecfg_parser.h"
#include "plant/heatsource.h"

#include "plant/plant.h"

int filecfg_heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* heatsource_parse_h */
