//
//  filecfg/backends_dump.h
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

void filecfg_backends_dump(void);
int filecfg_dump_tempid(const char *name, const tempid_t tempid);
int filecfg_dump_relid(const char *name, const relid_t relid);

#endif /* backends_dump_h */
