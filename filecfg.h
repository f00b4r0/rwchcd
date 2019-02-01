//
//  filecfg.h
//  rwchcd
//
//  (C) 2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File configuration interface API.
 */

#ifndef filecfg_h
#define filecfg_h

/** tabbed fprintf() */
#define tfprintf(file, il, format, ...)	fprintf(file, "%s" format, filecfg_tabs(il), ## __VA_ARGS__)

extern bool FCD_Exhaustive;

int filecfg_printf_wrapper(const bool indent, const char * restrict format, ...);

#define filecfg_printf(format, ...)	filecfg_printf_wrapper(false, format, ## __VA_ARGS__)
#define filecfg_iprintf(format, ...)	filecfg_printf_wrapper(true, format, ## __VA_ARGS__)

int filecfg_ilevel_inc(void);
int filecfg_ilevel_dec(void);

const char * filecfg_bool_str(const bool test);
const char * filecfg_runmode_str(const enum e_runmode runmode);

int filecfg_dump(void);

#endif /* filecfg_h */
