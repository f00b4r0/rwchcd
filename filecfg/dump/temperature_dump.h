//
//  filecfg/dump/temperature_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Temperature file configuration dumping API.
 */

#ifndef temperature_dump_h
#define temperature_dump_h

#include "temperature.h"

void filecfg_temperature_dump(const struct s_temperature * t);

#endif /* temperature_dump_h */
