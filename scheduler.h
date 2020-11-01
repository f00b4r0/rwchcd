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

#define SCHEDID_MAX	UINT_FAST16_MAX

/** Schedule entry time. */
struct s_schedule_etime {
	int wday;		///< day of the week for this schedule entry (`0` - `6`, Sunday = `0`)
	int hour;		///< hour of the day for this schedule entry (`0` - `23`)
	int min;		///< minute for this schedule entry (`0` - `59`)
};

/** Schedule entry parameters. */
struct s_schedule_eparams {
	enum e_runmode runmode;	///< target runmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	enum e_runmode dhwmode;	///< target dhwmode. @note #RM_UNKNOWN can be used to leave the current mode unchanged
	bool legionella;	///< true if legionella heat charge is requested
	bool recycle;		///< true if DHW recycle pump should be turned on
};

/** A schedule entry. Schedule entries are linked in a looped list. Config token 'entry' */
struct s_schedule_e {
	struct s_schedule_e * next;
	struct s_schedule_etime time;		///< time for this schedule entry
	struct s_schedule_eparams params;	///< parameters for this schedule entry
};

/**
 * A schedule.
 * Schedules are organized in linked list, NULL terminated.
 * Each schedule contains a looped list of schedule entries which are chronologically sorted.
 * The chronologically first entry is always available from the #head pointer.
 * The #current pointer, when set, points to the last valid schedule entry for the day.
 */
struct s_schedule {
	const struct s_schedule_e * _Atomic current;	///< current (valid) schedule entry (will be set once schedule has been parsed and sync'd to current day).
	struct s_schedule_e * head;		///< 'head' of sorted schedule entries loop (i.e. earliest schedule entry)
	const char * name;			///< schedule name (user-set unique identifier)
};

/** Schdules internal data */
struct s_schedules {
	struct s_schedule * all;	///< all registered schedules in the system
	schedid_t lastid;		///< id of last free schedule
	schedid_t n;			///< number of allocated schedules
};

void * scheduler_thread(void * arg);
const struct s_schedule_eparams * scheduler_get_schedparams(const schedid_t schedule_id);
const char * scheduler_get_schedname(const schedid_t schedule_id);
int scheduler_schedid_by_name(const char * const restrict sched_name);

int scheduler_add_entry(struct s_schedule * const sched, const struct s_schedule_e * const se);

void scheduler_exit(void);

#endif /* rwchcd_scheduler_h */
