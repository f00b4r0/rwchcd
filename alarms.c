//
//  alarms.c
//  rwchcd
//
//  (C) 2017-2018,2020-2021 Thibaut VARENE
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
 * @note the current implementation isn't quite best in class nor standard (for
 * instance the online() call takes an argument); it's a second-citizen in the codebase,
 * but it does the job for now.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>	// fork()/execv()

#include "alarms.h"
#include "timekeep.h"

/** alarm entry */
struct s_alarm {
	enum e_execs type;	///< error code
	char * restrict msg;	///< associated message
	int len;		///< #msg len
	struct s_alarm * next;	///< pointer to next entry
};

/** Alarms subsystem private data structure */
static struct s_alarms {
	bool online;		///< true if alarm system is online
	int count;		///< active alarms in the system
	struct s_alarm * alarm_head;	///< head pointer to the current list of alarms
	const char * notifier;	///< file executed when alarms are logged. Passed to `execvp()`, with a list of alarm messages as arguments
} Alarms = {
	.online = false,
	.count = 0,
	.alarm_head = NULL,
	.notifier = NULL,
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
 * @param format the printf-style format style
 * @return exec status
 */
int alarms_raise(const enum e_execs type, const char * restrict format, ...)
{
	struct s_alarm * restrict alarm;
	char * msg;
	va_list args;
	int ret;

	if (!Alarms.online)
		return (-EOFFLINE);

	if (!format)
		return (-EINVALID);

	va_start(args, format);
	ret = vasprintf(&msg, format, args);
	va_end(args);

	if (ret < 0)
		return (-EOOM);

	// create alarm
	alarm = calloc(1, sizeof(*alarm));
	if (!alarm) {
		free(msg);
		return (-EOOM);
	}

	// populate structure
	alarm->type = type;
	alarm->len = ret;
	alarm->msg = msg;

	// insert alarm at beginning of list
	alarm->next = Alarms.alarm_head;
	Alarms.alarm_head = alarm;
	Alarms.count++;

	return (ALL_OK);
}

/**
 * Init alarms subsystem.
 * @param notifier name/path that will be executed when alarms are logged
 * @return exec status
 */
int alarms_online(const char * notifier)
{
	Alarms.online = true;
	Alarms.notifier = notifier;
	return (ALL_OK);
}

/**
 * Run the alarms subsystem.
 * Currently only prints active alarms every 60s.
 * @return exec status
 * @todo revisit hardcoded throttle (60s)
 * @todo hash table, only print a given alarm once? Stateful alarms?
 */
int alarms_run(void)
{
	static timekeep_t last = 0;
	const char * argv[Alarms.count+1];
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

		argv[count] = NULL;
		while (alarm) {
			msg = alarm->msg;
			pr_log(_("\tALARM #%d: %s (%d)"), count--, msg, alarm->type);
			argv[count] = msg;	// alarms will be most recent last here (i.e. in natural order)
			alarm = alarm->next;
			last = now;
		}

		if (Alarms.notifier) {
			switch (fork()) {
				case 0:	// child
					execv(Alarms.notifier, argv);
					perror("Alarm notifier execution failed");	// execv() only returns on error
					break;
				case -1: // error - most likely ENOMEM
					perror(NULL);
					break;
				default: // parent
					break;
			}
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
