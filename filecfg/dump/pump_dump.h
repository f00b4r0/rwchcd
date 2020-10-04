//
//  filecfg/pump_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration dumping API.
 */

#ifndef pump_dump_h
#define pump_dump_h

#include "pump.h"

int filecfg_pump_dump(const struct s_pump * restrict const pump);

#endif /* pump_dump_h */
