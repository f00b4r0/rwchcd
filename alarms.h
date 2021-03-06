//
//  alarms.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Alarms implementation API.
 */

#ifndef rwchcd_alarms_h
#define rwchcd_alarms_h

#include "rwchcd.h"

int alarms_online(const char * notifier);
int alarms_count(void);
int alarms_raise(const enum e_execs type, const char * restrict format, ...);
int alarms_run(void);
void alarms_offline(void);

#endif /* rwchcd_alarms_h */
