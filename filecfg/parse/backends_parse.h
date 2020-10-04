//
//  filecfg/parse/backends_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration parsing API.
 */

#ifndef backends_parse_h
#define backends_parse_h

#include "filecfg_parser.h"
#include "hw_backends/hw_backends.h"

int filecfg_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

int filecfg_backends_parser_inid_parse(const enum e_hw_input_type type, void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_backends_parser_outid_parse(const enum e_hw_output_type type, void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* backends_parse_h */
