//
//  storage_filecfg.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Storage subsystem file configuration API.
 */

#ifndef storage_filecfg_h
#define storage_filecfg_h

#include "filecfg_parser.h"

int storage_filecfg_dump(void);
int storage_filecfg_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node);

#endif /* storage_filecfg_h */
