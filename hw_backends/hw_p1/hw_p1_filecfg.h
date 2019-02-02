//
//  hw_p1_filecfg.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 file configuration implementation.
 */

#ifndef hw_p1_filecfg_h
#define hw_p1_filecfg_h

#include "filecfg_parser.h"

int hw_p1_filecfg_dump(void * priv);
int hw_p1_filecfg_parse(const struct s_filecfg_parser_node * const node);

#endif /* hw_p1_filecfg_h */
