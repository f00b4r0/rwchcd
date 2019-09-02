//
//  scheduler.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * A very simple scheduler.
 * This scheduler is based on a weekly model. It currently only operates on
 * runtime-global runmode and dhwmode.
 * @todo adapt to be able to act on individual plant entities (and then handle e.g. DHWT's recycle pump)
 * @todo adapt to add "intelligence" and anticipation from e.g. circuit transitions
 * @bug subpar locking
 */

#include <stdlib.h>	// calloc
#include <unistd.h>	// sleep
#include <time.h>
#include <pthread.h>

#include "runtime.h"
#include "plant.h"	// for plant_dhwt_legionella_trigger()
#include "scheduler.h"
#include "filecfg.h"
#include "timekeep.h"

/** A schedule entry. Schedule entries are linked in a looped list. Config token 'entry' */
struct s_schedule_e {
	struct s_schedule_e * next;
	int tm_wday;		///< day of the week for this schedule entry (0 - 6, Sunday = 0)
	int tm_hour;		///< hour of the day for this schedule entry (0 - 23)
	int tm_min;		///< minute for this schedule entry (0 - 59)
	struct s_schedule_param p;	///< parameters for this schedule entry
};

/** A schedule list. The list loops, with last element pointing back to first */
static struct s_schedule {
	const struct s_schedule_e * current;	///< current (valid) schedule entry (will be set once schedule has been parsed and sync'd to current day)
	const struct s_schedule_e * head;	///< 'head' of sorted schedule entries loop (i.e. earliest schedule entry)
	const char * name;
	int schedid;
} Schedule;

/**
 * Find if the provided schedule entry is in a weekday's past time.
 * @param schent the schedule entry to test for
 * @param ltime a partially populated tm struct with valid weekday, hours and minutes.
 * @return TRUE if the schedule entry is in the same weekday as the provided ltime,
 * with hours and minutes before or exactly the same as that of the provided ltime,
 * FALSE in all other cases.
 */
static bool scheduler_ent_past_today(const struct s_schedule_e * const schent, const struct tm * const ltime)
{
	const int tm_wday = ltime->tm_wday;
	const int tm_hour = ltime->tm_hour;
	const int tm_min = ltime->tm_min;
	bool found = false;

	if (schent->tm_wday != tm_wday)
		goto end;

	// schent->tm_wday == tm_wday
	if (schent->tm_hour < tm_hour) {
		found = true;
		goto end;
	}
	else if (schent->tm_hour > tm_hour)
		goto end;

	// schent->tm_hour == tm_hour
	if (schent->tm_min <= tm_min)
		found = true;

end:
	return (found);
}

/**
 * Find the current schedule.
 * We parse the schedule list, updating the runmode and dhwmode variables
 * as we pass through past schedule entries. We stop when the next schedule entry
 * is in the future, which leaves us with the last valid schedule entry in sched->current.
 * @warning legionella trigger is run lockless
 * @return exec status
 * @bug if the first schedule of the day has either runmode OR dhwmode set to
 * #RM_UNKNOWN, the function will not look back to find the correct mode
 * (i.e. the current active mode will be unchanged).
 */
static int scheduler_now(void)
{
	struct s_runtime * const runtime = runtime_get();
	const time_t now = time(NULL);
	struct tm * const ltime = localtime(&now);	// localtime handles DST and TZ for us
	const struct s_schedule_e * schent, * schent_start;
	struct s_schedule * sched;
	enum e_runmode runmode, dhwmode, rt_runmode, rt_dhwmode;
	enum e_systemmode rt_sysmode;
	bool legionella, active;

	pthread_rwlock_rdlock(&runtime->runtime_rwlock);
	rt_sysmode = runtime->systemmode;
	rt_runmode = runtime->runmode;
	rt_dhwmode = runtime->dhwmode;
	pthread_rwlock_unlock(&runtime->runtime_rwlock);

	sched = &Schedule;

	if (SYS_AUTO != rt_sysmode) {
		sched->current = NULL;
		return (-EGENERIC);	// we can't update, no need to waste time
	}

	schent_start = sched->current ? sched->current : sched->head;
	active = sched->current ? true : false;	// used to flag an already active schedule

	if (!schent_start) {
		dbgmsg("empty schedule");
		return (-EGENERIC);	// empty schedule
	}

	// we have at least one schedule entry

restart:
	// find the current running schedule

	// special case first entry which may be the only one
	if (scheduler_ent_past_today(schent_start, ltime))
		sched->current = schent_start;

	// loop over other entries, stop if/when back at first entry
	for (schent = schent_start->next; schent && (schent_start != schent); schent = schent->next) {
		if (scheduler_ent_past_today(schent, ltime))
			sched->current = schent;
		else if (sched->current)
			break;	// if we already have a valid schedule entry ('synced'), first future entry stops search
	}

	// if we are already synced, check if we need to do anything
	if (sched->current) {
		if (active && (schent_start == sched->current))	// current schedule entry unchanged
			return (ALL_OK);	// nothing to update
	}
	else {
		/* we never synced and today's list didn't contain a single past schedule,
		 we must roll back through the week until we find one.
		 Set tm_hour and tm_min to last hh:mm of the (previous) day(s)
		 to find the last known valid schedule */
		ltime->tm_min = 59;
		ltime->tm_hour = 23;
		if (--ltime->tm_wday < 0)
			ltime->tm_wday = 6;
		goto restart;
	}

	// schedule entry was updated

	runmode = sched->current->p.runmode;
	dhwmode = sched->current->p.dhwmode;
	legionella = sched->current->p.legionella;

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
 * @todo improve inefficient time handling.
 */
void * scheduler_thread(void * arg)
{
	// start logging
	while (1) {
		scheduler_now();
		
		/* we poll every minute, this is not very efficient. Ideally we'd
		 * set a timer until the next schedule change, timer which could be
		 * updated by scheduler_add() if the added schedule comes before
		 * the currently scheduled wake. */
		timekeep_sleep(60);
	}
}


/**
 * Add a schedule entry.
 * Added entries are inserted at a sorted position.
 * @param tm_wday target day of the week (0 = Sunday = 7)
 * @param tm_hour target hour of the day (0 - 23)
 * @param tm_min target min of the hour (0 - 59)
 * @param runmode target runmode for this schedule entry
 * @param dhwmode target dhwmode for this schedule entry
 * @param legionella true if legionella charge should be triggered for this entry
 * @return exec status, -EEXISTS if entry is a time duplicate of another one
 */
int scheduler_add(int tm_wday, int tm_hour, int tm_min, enum e_runmode runmode, enum e_runmode dhwmode, bool legionella)
{
	struct s_schedule_e * sch = NULL, * sch_before, * sch_after, *sch_last;
	
	// sanity checks on params
	if ((tm_wday < 0) || (tm_wday > 7))
		return (-EINVALID);
	if (7 == tm_wday)		// convert sunday
		tm_wday = 0;
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
	sch_after = Schedule.head;
	
	// find insertion place
	if (sch_after) {
		do {
			if (sch_after->tm_wday == tm_wday) {
				if (sch_after->tm_hour == tm_hour) {
					if (sch_after->tm_min > tm_min)
						break;
					else if (sch_after->tm_min == tm_min)
						goto duplicate;
				}
				else if (sch_after->tm_hour > tm_hour)
					break;
			}
			else if (sch_after->tm_wday > tm_wday)
				break;

			sch_before = sch_after;
			sch_after = sch_before->next;
		} while (Schedule.head != sch_after);

		// if we're going to replace the head, we must find the last element
		if (!sch_before && (Schedule.head == sch_after))
			for (sch_last = sch_after; Schedule.head != sch_last->next; sch_last = sch_last->next);
	}
	else
		sch_last = sch;		// new entry is the only and last one

	sch->tm_wday = tm_wday;
	sch->tm_hour = tm_hour;
	sch->tm_min = tm_min;
	sch->p.runmode = runmode;
	sch->p.dhwmode = dhwmode;
	sch->p.legionella = legionella;

	/* Begin fence section.
	 * XXX REVISIT memory order is important here for this code to work reliably
	 * lockless. We probably need a fence. This is not "mission critical" so
	 * I'll leave it as is for now. */
	sch->next = sch_after;	// if sch_after == NULL (can only happen with sch_before NULL too), will be updated via sch_last
	
	if (!sch_before)
		sch_last->next = Schedule.head = sch;
	else
		sch_before->next = sch;

	Schedule.current = NULL;	// desync
	/* End fence section */
	
//	dbgmsg("add schedule. tm_wday: %d, tm_hour: %d, tm_min: %d, runmode: %d, dhwmode: %d, legionella: %d",
//	       tm_wday, tm_hour, tm_min, runmode, dhwmode, legionella);
	
	return (ALL_OK);

duplicate:
	free(sch);
	return (-EEXISTS);
}

static void scheduler_entry_dump(const struct s_schedule_e * const schent)
{
	if (!schent)
		return;

	filecfg_iprintf("entry {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("wday %d;\n", schent->tm_wday);		// mandatory
	filecfg_iprintf("hour %d;\n", schent->tm_hour);		// mandatory
	filecfg_iprintf("min %d;\n", schent->tm_min);		// mandatory
	if (RM_UNKNOWN != schent->p.runmode)
		filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(schent->p.runmode));
	if (RM_UNKNOWN != schent->p.dhwmode)
		filecfg_iprintf("dhwmode \"%s\";\n", filecfg_runmode_str(schent->p.runmode));
	if (schent->p.legionella)
		filecfg_iprintf("legionella %s;\n", filecfg_bool_str(schent->p.legionella));
	if (schent->p.recycle)
		filecfg_iprintf("recycle %s;\n", filecfg_bool_str(schent->p.recycle));

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump the full schedule to config file.
 * @return exec status
 * @warning not thread safe
 */
int scheduler_filecfg_dump(void)
{
	const struct s_schedule_e * schent, * schent_start;

	filecfg_iprintf("scheduler {\n");
	filecfg_ilevel_inc();

	if (!Schedule.head)
		goto emptysched;

	schent_start = Schedule.head;
	scheduler_entry_dump(schent_start);
	for (schent = schent_start->next; schent && (schent_start != schent); schent = schent->next)
		scheduler_entry_dump(schent);

emptysched:
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
