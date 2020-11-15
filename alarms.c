//
//  alarms.c
//  rwchcd
//
//  (C) 2017-2018,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Alarms implementation.
 * This file implements a basic alarms subsystem.
 *
 * Considering we're running a loop, we can implement a stateless alarm system:
 * at every iteration of the loop, each section of the code that needs to raise
 * an alarm can do so. The alarms are collected and at the "end" of the loop
 * iteration the existing alarms are cleared (to be set again if still present
 * at the next iteration).
 * The advantage is that there's no need to track the alarms to avoid duplication,
 * the system can remain lightweight. The inconvenient is there's a single point
 * in time where all the alarms are fully collected before being deleted. This
 * happens in alarms_run(). alarms_count() is provided
 * for convenience but should only be used immediately before alarms_run().
 * The other inconvenient is that spurious alarms (that happen once and go away)
 * will be reported. Then again, those /should not/ happen in the first place.
 *
 * @todo implement a notifier (exec() some external script?)
 */

#include <stdlib.h>
#include <string.h>

#include "alarms.h"
#include "timekeep.h"

/** alarm entry */
struct s_alarm {
	//uintptr_t identifier;	///< optional unique identifier
	//int level;
	enum e_execs type;	///< error code
	char * restrict msg;	///< associated message (optional)
	struct s_alarm * next;	///< pointer to next entry
};

/** Alarms subsystem private data structure */
static struct s_alarms {
	bool online;		///< true if alarm system is online
	int count;		///< active alarms in the system
	struct s_alarm * alarm_head;	///< head pointer to the current list of alarms
} Alarms = {
	.online = false,
	.count = 0,
	.alarm_head = NULL,
}; ///< Alarms subsystem private data

/**
 * Clear all alarms.
 */
static void alarms_clear(void)
{
	struct s_alarm * alarm, * next;

	// clear all registered alarms
	alarm = Alarms.alarm_head;
	while (alarm) {
		next = alarm->next;
		free(alarm->msg);
		free(alarm);
		alarm = next;
	}

	Alarms.count = 0;
	Alarms.alarm_head = NULL;
}

/**
 * Check if one (or more) alarm condition exists in the system.
 * @return number of active alarms
 */
int alarms_count(void)
{
	return (Alarms.count);
}

/**
 * Raise an alarm in the system.
 * Alarm is added at the beginning of the list: last alarm is always first in the list.
 * @param type alarm error code
 * @param msg mandatory message string; a local copy is made
 * @return exec status
 */
int alarms_raise(const enum e_execs type, const char * const msg)
{
	struct s_alarm * restrict alarm = NULL;

	if (!Alarms.online)
		return (-EOFFLINE);

	if (!msg)
		return (-EINVALID);

	// create alarm
	alarm = calloc(1, sizeof(*alarm));
	if (!alarm)
		return (-EOOM);

	// populate structure
	alarm->type = type;
	if (msg)
		alarm->msg = strdup(msg);

	// insert alarm at beginning of list
	alarm->next = Alarms.alarm_head;
	Alarms.alarm_head = alarm;
	Alarms.count++;

	return (ALL_OK);
}

/**
 * Init alarms subsystem.
 * @return exec status
 */
int alarms_online(void)
{
	Alarms.online = true;
	return (ALL_OK);
}

/**
 * Run the alarms subsystem.
 * Currently only prints active alarms every 60s.
 * @return exec status
 * @bug hardcoded throttle (60s)
 * @todo hash table, only print a given alarm once? Stateful alarms?
 */
int alarms_run(void)
{
	static timekeep_t last = 0;
	const timekeep_t now = timekeep_now();
	const timekeep_t dt = now - last;
	const struct s_alarm * alarm;
	const char * msg;
	int count;

	if (unlikely(!Alarms.online))
		return (-EOFFLINE);

	if (!Alarms.count)	// no active alarm, can stop here
		return (ALL_OK);

	if (dt >= timekeep_sec_to_tk(60)) {
		alarm = Alarms.alarm_head;
		count = Alarms.count;

		pr_log(_("Alarms active in the system (%d), most recent first:"), count);

		while (alarm) {
			msg = alarm->msg;
			pr_log(_("\tALARM #%d: %s"), count--, msg);
			alarm = alarm->next;
			last = now;
		}
	}

	// must clear active alarms after every run otherwise they would be duplicated
	alarms_clear();

	return (ALL_OK);
}

/**
 * Exit alarms subsystem.
 */
void alarms_offline(void)
{
	Alarms.online = false;

	alarms_clear();
}
