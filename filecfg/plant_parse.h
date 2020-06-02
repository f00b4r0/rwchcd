//
//  filecfg/plant_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant file configuration parsing API.
 */

#ifndef plant_parse_h
#define plant_parse_h

#include "filecfg_parser.h"

int filecfg_plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* plant_parse_h */
