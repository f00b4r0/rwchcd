//
//  filecfg/parse/switch_parse.h
//  rwchcd
//
//  (C) 2023 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global switch system parsing API.
 */

#ifndef switch_parse_h
#define switch_parse_h

int filecfg_switch_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* switch_parse_h */
