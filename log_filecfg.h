//
//  log_filecfg.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration API.
 */

#ifndef log_filecfg_h
#define log_filecfg_h

#include "filecfg_parser.h"

int log_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int log_filecfg_dump(void);

#endif /* log_filecfg_h */
