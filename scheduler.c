//
//  scheduler.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * A very simple scheduler.
 * This scheduler is based on a weekly model. It currently only operates on
 * runtime-global runmode and dhwmode.
 * @todo adapt to be able to act on individual plant entities
 * @todo adapt to add "intelligence" and anticipation from e.g. circuit transitions
 * @bug subpar locking
 */

#include <stdlib.h>	// calloc
#include <unistd.h>	// sleep
#include <time.h>
#include <pthread.h>

#include "runtime.h"
#include "plant.h"
#include "scheduler.h"

/** A schedule item for a given day. */
struct s_schedule {
	int tm_hour;		///< hour of the day for this schedule (0 - 23)
	int tm_min;		///< minute for this schedule	(0 - 59)
	enum e_runmode runmode;	///< target runmode. @note Invalid runmode could be used to leave the current mode unchanged
	enum e_runmode dhwmode;	///< target dhwmode. @note Invalid dhwmode could be used to leave the current mode unchanged
	bool legionella;	///< true if legionella heat charge is requested
	struct s_schedule * next;
};

/** Array of daily schedules for the week */
static struct s_schedule * Schedule_week[7] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, };

/**
 * Find the current schedule.
 * We parse today's schedule list, updating the runmode and dhwmode variables
 * as we pass through past schedules. We stop when the next schedule is in the
 * future, which leaves us with the last valid run/dhw modes in the variables.
 * @bug if the first schedule of the day has either runmode OR dhwmode set to
 * RM_UNKNOWN, the function will not look back to find the correct mode.
 * @warning legionella trigger is run lockless
 * @return exec status
 */
static int scheduler_now(void)
{
	struct s_runtime * const runtime = runtime_get();
	const time_t now = time(NULL);
	const struct tm * const ltime = localtime(&now);	// localtime handles DST and TZ for us
	const struct s_schedule * sch;
	int tm_wday = ltime->tm_wday;
	int tm_hour = ltime->tm_hour;
	int tm_min = ltime->tm_min;
	const int tm_wday_start = tm_wday;
	enum e_runmode runmode, dhwmode, rt_runmode, rt_dhwmode;
	enum e_systemmode rt_sysmode;
	bool legionella;
	bool found = false;

	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	rt_sysmode = runtime->systemmode;
	rt_runmode = runtime->runmode;
	rt_dhwmode = runtime->dhwmode;
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	if (SYS_AUTO != rt_sysmode)
		return (-EGENERIC);	// we can't update, no need to waste time

	// start from invalid mode (prevents spurious change with runtime_set_*())
	runmode = dhwmode = RM_UNKNOWN;
	legionella = false;
	
restart:
	sch = Schedule_week[tm_wday];
	
	// find the current running schedule
	while (sch) {
		if (sch->tm_hour < tm_hour) {
			if (RM_UNKNOWN != sch->runmode)	// only update mode if valid
				runmode = sch->runmode;
			if (RM_UNKNOWN != sch->dhwmode)
				dhwmode = sch->dhwmode;
			legionella = sch->legionella;
			sch = sch->next;
			found = true;
			continue;
		}
		else if (sch->tm_hour == tm_hour) {	// same hour, must check minutes
			if (sch->tm_min <= tm_min) {
				if (RM_UNKNOWN != sch->runmode)	// only update mode if valid
					runmode = sch->runmode;
				if (RM_UNKNOWN != sch->dhwmode)
					dhwmode = sch->dhwmode;
				legionella = sch->legionella;
				sch = sch->next;
				found = true;
				continue;
			}
			else
				break;
		}
		else // (sch->tm_hour > tm_hour): FUTURE
			break;
	}
	
	if (!found) {
		/* today's list didn't contain a single past schedule,
		 we must roll back through the week until we find one.
		 Set tm_hour and tm_min to last hh:mm of the (previous) day(s)
		 to find the last known valid schedule */
		tm_min = 59;
		tm_hour = 23;
		if (--tm_wday < 0)
			tm_wday = 6;
		if (tm_wday_start == tm_wday) {
			dbgmsg("no schedule found");
			return (-EGENERIC);	// empty schedule
		}
		goto restart;
	}

	// update only if necessary
	if ((runmode != rt_runmode) || (dhwmode != rt_dhwmode)) {
		dbgmsg("schedule update at %ld. Runmode old: %d, new: %d; dhwmode old: %d, new: %d",
		       now, rt_runmode, runmode, rt_dhwmode, dhwmode);
		pthread_rwlock_wrlock(&runtime->runtime_rwlock);
		runtime_set_dhwmode(dhwmode);	// errors ignored (if invalid mode or if sysmode != AUTO)
		runtime_set_runmode(runmode);
		pthread_rwlock_unlock(&runtime->runtime_rwlock);
	}
	if (legionella)
		plant_dhwt_legionella_trigger(runtime->plant);	// XXX lockless should work

	return (ALL_OK);
}

/**
 * Simple scheduler thread.
 * runs a delay loop through the callbacks.
 * @bug buggy time handling.
 */
void * scheduler_thread(void * arg)
{
	// start logging
	while (1) {
		scheduler_now();
		
		/* we poll every minute, this is very inefficient. Ideally we'd
		 * set a timer until the next schedule change, timer which could be
		 * updated by scheduler_add() if the added schedule comes before
		 * the currently scheduled wake. */
		sleep(60);
	}
}


/**
 * Add a schedule entry.
 * @param tm_wday target day of the week (0 = Sunday)
 * @param tm_hour target hour of the day (0 - 23)
 * @param tm_min target min of the hour (0 - 59)
 * @param runmode target runmode for this schedule entry
 * @param dhwmode target dhwmode for this schedule entry
 */
int scheduler_add(int tm_wday, int tm_hour, int tm_min, enum e_runmode runmode, enum e_runmode dhwmode)
{
	struct s_schedule * sch = NULL, * sch_before, * sch_after;
	
	// sanity checks on params
	if ((tm_wday < 0) || (tm_wday > 6))
		return (-EINVALID);
	if ((tm_hour < 0) || (tm_hour > 23))
		return (-EINVALID);
	if ((tm_min < 0) || (tm_min > 59))
		return (-EINVALID);
	if (runmode > RM_UNKNOWN)
		return (-EINVALID);
	if (dhwmode > RM_UNKNOWN)
		return (-EINVALID);
	
	sch = calloc(1, sizeof(*sch));
	if (!sch)
		return (-EOOM);
	
	sch_before = NULL;
	sch_after = Schedule_week[tm_wday];
	
	// find insertion place
	while (sch_after) {
		if (sch_after->tm_hour == tm_hour) {
			if (sch_after->tm_min > tm_min)
				break;
		}
		else if (sch_after->tm_hour > tm_hour)
			break;
		
		sch_before = sch_after;
		sch_after = sch_before->next;
	}
	
	sch->tm_hour = tm_hour;
	sch->tm_min = tm_min;
	sch->runmode = runmode;
	sch->dhwmode = dhwmode;
	
	/* Begin fence section.
	 * XXX REVISIT memory order is important here for this code to work reliably
	 * lockless. We probably need a fence. This is not "mission critical" so
	 * I'll leave it as is for now. */
	sch->next = sch_after;
	
	if (!sch_before)
		Schedule_week[tm_wday] = sch;
	else
		sch_before->next = sch;
	/* End fence section */
	
	dbgmsg("add schedule. tm_wday: %d, tm_hour: %d, tm_min: %d, runmode: %d, dhwmode: %d",
	       tm_wday, tm_hour, tm_min, runmode, dhwmode);
	
	return (ALL_OK);
}

