//
//  filecfg/relay_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global relay system parsing API.
 */


#ifndef relay_parse_h
#define relay_parse_h

int filecfg_relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* relay_parse_h */
