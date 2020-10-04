//
//  filecfg/parse/temperature_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global temperature system parsing API.
 */


#ifndef temperature_parse_h
#define temperature_parse_h

int filecfg_temperature_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* temperature_parse_h */
