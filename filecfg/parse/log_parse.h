//
//  filecfg/parse/log_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration parsing API.
 */

#ifndef log_parse_h
#define log_parse_h

#include "filecfg_parser.h"

int filecfg_log_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* log_parse_h */
