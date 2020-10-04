//
//  filecfg/dump/heatsource_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource file configuration dumping API.
 */

#ifndef heatsource_dump_h
#define heatsource_dump_h

#include "heatsource.h"

int filecfg_heatsource_dump(const struct s_heatsource * restrict const heat);

#endif /* heatsource_dump_h */
