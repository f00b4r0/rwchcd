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

struct s_schedule_param {
	enum e_runmode runmode;	///< target runmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	enum e_runmode dhwmode;	///< target dhwmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	bool legionella;	///< true if legionella heat charge is requested
	bool recycle;		///< true if DHW recycle pump should be turned on
};

void * scheduler_thread(void * arg);
const struct s_schedule_param * scheduler_get_schedparams(const int schedule_id);
int scheduler_schedid_by_name(const char * const restrict sched_name);
int scheduler_add_entry(int schedid, int tm_wday, int tm_hour, int tm_min, enum e_runmode runmode, enum e_runmode dhwmode, bool legionella);
int scheduler_add_schedule(const char * const restrict name);
int scheduler_filecfg_dump(void);

#endif /* rwchcd_scheduler_h */
