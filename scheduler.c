//
//  scheduler.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler subsystem.
 * This scheduler is based on a weekly model. It updates a pool of weekly schedules to keep them
 * up to the current date. Interfaces are provided to give (read) access to the schedule's current setup,
 * enabling individual plant entities to follow a custom schedule.
 * Configuration of the scheduler happens in a `scheduler` root node in the configuration file which contains
 * one or more named `schedule` nodes, themselves containing one or more `entry` nodes composed of a
 * `time` node (content from struct s_schedule_etime) and a `params` node (content from struct s_schedule_eparams).
 * The name of the `schedule` node(s) can then be used to assign various plant entities to the given schedule.
 * @todo adapt to add "intelligence" and anticipation from e.g. circuit transitions
 * @note Operation is lockless as it is assumed that the schedules will only be updated at config time
 * (during startup in single-thread context) and that from that point on only read operations will be performed,
 * until shutdown (also in single-threaded context). Should that change, adequate mutex constructs must be used.
 */

#include <stdlib.h>	// calloc
#include <unistd.h>	// sleep
#include <time.h>
#include <string.h>	// strcmp/memcpy
#include <stdatomic.h>
#include <limits.h>
#include <assert.h>

#include "scheduler.h"
#include "timekeep.h"

struct s_schedules Schedules = {
	NULL,
	0,
};

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

	if (schent->time.wday != tm_wday)
		goto end;

	// schent->time.wday == tm_wday
	if (schent->time.hour < tm_hour) {
		found = true;
		goto end;
	}
	else if (schent->time.hour > tm_hour)
		goto end;

	// schent->time.hour == tm_hour
	if (schent->time.min <= tm_min)
		found = true;

end:
	return (found);
}

/**
 * Update a schedule to its most current entry.
 * This function updates the #sched->current pointer to the passed schedule to
 * its most current entry, if any. We stop when the next schedule entry
 * is in the future, which leaves us with the last valid schedule entry in #sched->current.
 * @param sched the schedule to update.
 * @warning if the first schedule of the day has either runmode OR dhwmode set to
 * #RM_UNKNOWN, the function will not look back to find the correct mode
 * (i.e. the current active mode will be unchanged).
 */
static void scheduler_update_schedule(struct s_schedule * const sched)
{
	const time_t now = time(NULL);
	struct tm * const ltime = localtime(&now);	// localtime handles DST and TZ for us
	const struct s_schedule_e * schent, * schent_start, * schent_curr;

	schent_curr = aler(&sched->current);

	schent_start = likely(schent_curr) ? schent_curr : sched->head;

	if (unlikely(!schent_start)) {
		dbgmsg(1, 1, "empty schedule");
		return;
	}

	// we have at least one schedule entry

restart:
	// find the current running schedule

	// special case first entry which may be the only one
	if (scheduler_ent_past_today(schent_start, ltime))
		aser(&sched->current, schent_start);

	// loop over other entries, stop if/when back at first entry
	for (schent = schent_start->next; schent_start != schent; schent = schent->next) {
		if (unlikely(scheduler_ent_past_today(schent, ltime)))
			aser(&sched->current, schent);
		else if (likely(schent_curr))
			break;	// if we already have a valid schedule entry ('synced'), first future entry stops search
	}

	// if we aren't already synced, try harder
	if (unlikely(!aler(&sched->current))) {
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
}

/**
 * Update all schedules.
 */
static int scheduler_now(void)
{
	struct s_schedule * sched;

	for (sched = Schedules.schead; sched; sched = sched->next)
		scheduler_update_schedule(sched);

	return (ALL_OK);
}

/**
 * Find a schedule by identifier.
 * @param schedule_id the target id
 * @return schedule id if found, error otherwise.
 */
static struct s_schedule * scheduler_schedule_fbi(const schedid_t schedule_id)
{
	struct s_schedule * sched;

	if (unlikely((schedule_id <= 0) || (schedule_id > Schedules.lastid)))
		return (NULL);

	// find correct schedule
	for (sched = Schedules.schead; sched; sched = sched->next) {
		if (schedule_id == sched->schedid)
			break;
	}

	return (sched);
}

/**
 * Return a pointer to the current valid parameters for a given schedule id.
 * @param schedid the target schedule id
 * @return pointer to params if found, NULL otherwise.
 */
const struct s_schedule_eparams * scheduler_get_schedparams(const schedid_t schedule_id)
{
	const struct s_schedule * sched;
	const struct s_schedule_e * schent_curr;

	sched = scheduler_schedule_fbi(schedule_id);

	// return current schedule entry for schedule, if available
	if (likely(sched)) {
		schent_curr = aler(&sched->current);
		if (likely(schent_curr))
			return (&schent_curr->params);
	}

	return (NULL);
}


/**
 * Return the name of a given schedule id.
 * @param schedid the target schedule id
 * @return name if found, NULL otherwise.
 */
const char * scheduler_get_schedname(const schedid_t schedule_id)
{
	const struct s_schedule * sched;

	sched = scheduler_schedule_fbi(schedule_id);

	if (sched)
		return (sched->name);
	else
		return (NULL);
}

/**
 * Find the schedid of a named schedule.
 * @param sched_name the schedule name to match
 * @return schedid if found, negative error otherwise
 */
int scheduler_schedid_by_name(const char * const restrict sched_name)
{
	const struct s_schedule * sched;
	int ret = -ENOTFOUND;

	if (!sched_name)
		return (-EINVALID);

	for (sched = Schedules.schead; sched; sched = sched->next) {
		if (!strcmp(sched_name, sched->name)) {
			assert(sched->schedid < INT_MAX);
			ret = (int)sched->schedid;
			break;
		}
	}

	return (ret);
}

/**
 * Simple scheduler thread.
 * runs a delay loop through the callbacks.
 * @todo improve inefficient time handling.
 */
void * scheduler_thread(void * arg __attribute__((unused)))
{
	// start logging
	while (1) {
		scheduler_now();
		
		/* we poll every minute, this is not very efficient. Ideally we'd
		 * set a timer until the next schedule change, timer which could be
		 * updated by scheduler_add_entry() if the added schedule comes before
		 * the currently scheduled wake. */
		timekeep_sleep(60);
	}
}

/**
 * Add a new schedule.
 * @param name the new schedule name (must be unique).
 * @return new schedule id or negative error.
 * @warning not thread safe
 */
int scheduler_add_schedule(const char * const restrict name)
{
	struct s_schedule * sched;
	char * str = NULL;

	// sanitize input: check that mandatory callbacks are provided
	if (!name)
		return (-EINVALID);

	// name must be unique
	if (scheduler_schedid_by_name(name) > 0)
		return (-EEXISTS);

	// clone name
	str = strdup(name);
	if (!str)
		return(-EOOM);

	// allocate new backend
	sched = calloc(1, sizeof(*sched));
	if (!sched) {
		free(str);
		return (-EOOM);
	}

	// populate schedule
	sched->name = str;
	sched->schedid = ++Schedules.lastid;

	// insert schedule into list - XXX MEMORY FENCE
	sched->next = Schedules.schead;
	Schedules.schead = sched;

	// return schedule id
	assert(sched->schedid < INT_MAX);
	return ((int)sched->schedid);
}

/**
 * Add a schedule entry.
 * Added entries are inserted at a sorted position.
 * @param schedid id of the schedule to add entry to.
 * @param se template for the new schedule entry
 * @return exec status, -EEXISTS if entry is a time duplicate of another one
 * @warning not thread safe
 */
int scheduler_add_entry(const schedid_t schedid, const struct s_schedule_e * const se)
{
	struct s_schedule * sched;
	struct s_schedule_e * schent = NULL, * schent_before, * schent_after, * schent_last;
	
	// sanity checks on params
	if (!se)
		return (-EINVALID);

	if ((se->time.wday < 0) || (se->time.wday > 6))
		return (-EINVALID);
	if ((se->time.hour < 0) || (se->time.hour > 23))
		return (-EINVALID);
	if ((se->time.min < 0) || (se->time.min > 59))
		return (-EINVALID);

	if (se->params.runmode > RM_UNKNOWN)
		return (-EINVALID);
	if (se->params.dhwmode > RM_UNKNOWN)
		return (-EINVALID);

	sched = scheduler_schedule_fbi(schedid);
	if (!sched)
		return (-ENOTFOUND);
	
	schent = calloc(1, sizeof(*schent));
	if (!schent)
		return (-EOOM);
	
	schent_before = NULL;
	schent_after = sched->head;
	
	// find insertion place
	if (schent_after) {
		do {
			if (se->time.wday == schent_after->time.wday) {
				if (se->time.hour == schent_after->time.hour) {
					if (se->time.min < schent_after->time.min)
						break;
					else if (se->time.min == schent_after->time.min)
						goto duplicate;
				}
				else if (se->time.hour < schent_after->time.hour)
					break;
			}
			else if (se->time.wday < schent_after->time.wday)
				break;

			schent_before = schent_after;
			schent_after = schent_before->next;
		} while (sched->head != schent_after);

		// if we're going to replace the head, we must find the last element
		if (!schent_before)
			for (schent_last = schent_after; sched->head != schent_last->next; schent_last = schent_last->next);
	}
	else
		schent_last = schent;		// new entry is the only and last one

	memcpy(schent, se, sizeof(*schent));

	/* Begin fence section.
	 * XXX REVISIT memory order is important here for this code to work reliably
	 * lockless. We probably need a fence. This is not "mission critical" so
	 * I'll leave it as is for now. */
	schent->next = schent_after;	// if sch_after == NULL (can only happen with sch_before NULL too), will be updated via sch_last
	
	if (!schent_before)
		schent_last->next = sched->head = schent;
	else
		schent_before->next = schent;

	sched->current = NULL;	// desync
	/* End fence section */

	return (ALL_OK);

duplicate:
	free(schent);
	return (-EEXISTS);
}
