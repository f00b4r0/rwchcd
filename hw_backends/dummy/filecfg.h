//
//  hw_backends/dummy/filecfg.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Dummy backend file configuration API.
 */

#ifndef filecfg_h
#define filecfg_h

#include "filecfg_parser.h"

int dummy_filecfg_parse(const struct s_filecfg_parser_node * const node);

#endif /* filecfg_h */
