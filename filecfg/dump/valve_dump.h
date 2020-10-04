//
//  filecfg/dump/valve_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve subsystem file configuration dumping API.
 */

#ifndef valve_dump_h
#define valve_dump_h

#include "plant/valve.h"

int filecfg_valve_dump(const struct s_valve * restrict const valve);

#endif /* valve_dump_h */
