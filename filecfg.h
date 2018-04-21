//
//  filecfg.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
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

const char * filecfg_tabs(const unsigned int level);
int filecfg_dump(void);

#endif /* filecfg_h */