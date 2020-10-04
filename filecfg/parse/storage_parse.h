//
//  filecfg/parse/storage_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Storage subsystem file configuration parsing API.
 */

#ifndef storage_parse_h
#define storage_parse_h

#include "filecfg_parser.h"

int filecfg_storage_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node);

#endif /* storage_parse_h */
