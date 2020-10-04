//
//  filecfg/dump/backends_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration dumping API.
 */

#ifndef backends_dump_h
#define backends_dump_h

#include "rwchcd.h"
#include "hw_backends.h"

void filecfg_backends_dump(void);
int filecfg_backends_dump_binid(const enum e_hw_input_type type, const char *name, const binid_t tempid);
int filecfg_backends_dump_boutid(const enum e_hw_output_type type, const char *name, const boutid_t boutid);

#define filecfg_backends_dump_temperature(name, tempid)	filecfg_backends_dump_binid(HW_INPUT_TEMP, name, tempid)
#define filecfg_backends_dump_relay(name, relid)	filecfg_backends_dump_boutid(HW_OUTPUT_RELAY, name, relid)

#endif /* backends_dump_h */
