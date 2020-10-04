//
//  filecfg/dhwt_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT file configuration parsing API.
 */

#ifndef dhwt_parse_h
#define dhwt_parse_h

#include "filecfg_parser.h"

int filecfg_dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* dhwt_parse_h */
