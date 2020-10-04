//
//  filecfg/parse/scheduler_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler subsystem file configuration parsing API.
 */


#ifndef scheduler_parse_h
#define scheduler_parse_h

#include "filecfg_parser.h"

int filecfg_scheduler_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* scheduler_parse_h */
