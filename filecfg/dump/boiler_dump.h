//
//  filecfg/dump/boiler_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler heatsource file configuration dumping API.
 */

#ifndef boiler_dump_h
#define boiler_dump_h

#include "plant/heatsource.h"

int filecfg_boiler_hs_dump(const struct s_heatsource * restrict const heat);

#endif /* boiler_dump_h */
