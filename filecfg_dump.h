//
//  filecfg_dump.h
//  rwchcd
//
//  (C) 2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File configuration dump interface API.
 */

#ifndef filecfg_dump_h
#define filecfg_dump_h

#include "rwchcd.h"

extern bool FCD_Exhaustive;	///< If true, the exhaustive configuration will be printed (includes unconfigured fields).

int filecfg_printf_wrapper(const bool indent, const char * restrict format, ...);

#define filecfg_printf(format, ...)	filecfg_printf_wrapper(false, format, ## __VA_ARGS__)	///< non-indented fprintf
#define filecfg_iprintf(format, ...)	filecfg_printf_wrapper(true, format, ## __VA_ARGS__)	///< auto-indented fprintf

int filecfg_ilevel_inc(void);
int filecfg_ilevel_dec(void);

const char * filecfg_runmode_str(const enum e_runmode runmode);

int filecfg_dump_tempid(const char *name, const tempid_t tempid);
int filecfg_dump_relid(const char *name, const relid_t relid);
int filecfg_dump_nodebool(const char *name, bool value);
int filecfg_dump_nodestr(const char *name, const char *value);
int filecfg_dump_celsius(const char *name, temp_t value);
int filecfg_dump_deltaK(const char *name, temp_t value);
int filecfg_dump_tk(const char *name, timekeep_t value);

int filecfg_dump(void);

#endif /* filecfg_dump_h */
