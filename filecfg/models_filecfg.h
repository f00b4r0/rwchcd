//
//  models_filecfg.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models subsystem file configuration API.
 */

#ifndef models_filecfg_h
#define models_filecfg_h

#include "filecfg_parser.h"

int models_filecfg_dump(void);
int models_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* models_filecfg_h */
