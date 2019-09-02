//
//  scheduler.h
//  rwchcd
//
//  (C) 2016,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler API
 */

#ifndef rwchcd_scheduler_h
#define rwchcd_scheduler_h

#include "rwchcd.h"

/** Schedule entry time. */
struct s_schedule_etime {
	int wday;		///< day of the week for this schedule entry (0 - 6, Sunday = 0)
	int hour;		///< hour of the day for this schedule entry (0 - 23)
	int min;		///< minute for this schedule entry (0 - 59)
};

/** Schedule entry parameters. */
struct s_schedule_eparams {
	enum e_runmode runmode;	///< target runmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	enum e_runmode dhwmode;	///< target dhwmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	bool legionella;	///< true if legionella heat charge is requested
	bool recycle;		///< true if DHW recycle pump should be turned on
};

void * scheduler_thread(void * arg);
const struct s_schedule_eparams * scheduler_get_schedparams(const schedid_t schedule_id);
int scheduler_schedid_by_name(const char * const restrict sched_name);
int scheduler_add_entry(const schedid_t schedid, const struct s_schedule_etime * const etime, const struct s_schedule_eparams * const eparams);
int scheduler_add_schedule(const char * const restrict name);
int scheduler_filecfg_dump(void);

#endif /* rwchcd_scheduler_h */
