//
//  scheduler.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler API
 */

#ifndef rwchcd_scheduler_h
#define rwchcd_scheduler_h

#include "rwchcd.h"

void * scheduler_thread(void * arg);
int scheduler_add(int tm_wday, int tm_hour, int tm_min, enum e_runmode runmode, enum e_runmode dhwmode);

#endif /* rwchcd_scheduler_h */
