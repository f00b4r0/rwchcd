//
//  rwchcd_alarms.c
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Alarms implementation.
 * This file implements basic alarms subsystem.
 *
 * Considering we're running a loop, we can implement a stateless alarm system:
 * at every iteration of the loop, each section of the code that needs to raise
 * an alarm can do so. The alarms are collected and at the "end" of the loop
 * iteration the existing alarms are cleared (to be set again if still present
 * at the next iteration).
 * The advantage is that there's no need to track the alarms to avoid duplication,
 * the system can remain lightweight. The inconvenient is there's a single point
 * in time where all the alarms are fully collected before being deleted. This
 * happens in alarms_run(). alarms_active() and alarms_msg_iterator() are provided
 * for convenience but should only be used immediately before alarms_run().
 */

#include <stdlib.h>
#include <string.h>

#include "rwchcd.h"
#include "rwchcd_alarms.h"

/** alarm entry */
struct s_alarm {
	//uintptr_t identifier;	///< optional unique identifier
	//int level;
	enum e_execs type;	///< error code
	char * msg;		///< associated message (optional)
	char * msglcd;		///< associated message (optional), @note @b LCD_LINELEN chars MAX
	struct s_alarm * next;	///< pointer to next entry
};

/** List of all existing alarms in the system */
static struct s_alarms {
	bool online;		///< true if alarm system is online
	int count;		///< active alarms in the system
	struct s_alarm * alarm_head;
} Alarms = {
	.online = false,
	.count = 0,
	.alarm_head = NULL,
};

#if 0
/**
 * Find an alarm entry by identifier.
 * @param identifier the target identifier
 * @return the matching alarm entry if any (or NULL)
 */
static struct s_alarm * alarm_find(const uintptr_t identifier)
{
	struct s_alarm * restrict alarm;

	for (alarm = Alarms.alarm_head; alarm; alarm = alarm->next) {
		if (identifier == alarm->identifier)
			break;
	}

	return (alarm);
}
#endif

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
		free(alarm->msglcd);
		free(alarm);
		alarm = next;
	}

	Alarms.count = 0;
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
 * Iterate over current system alarms messages.
 * @param msglcd if true, short message will be returned.
 * @return alarm message (if any)
 */
const char * alarms_msg_iterator(const bool msglcd)
{
	static const struct s_alarm * restrict alarm = NULL;
	const char * msg;

	if (!Alarms.online)
		return (NULL);

	if (!alarm)
		alarm = Alarms.alarm_head;

	if (!alarm)
		return (NULL);

	msg = msglcd ? alarm->msglcd : alarm->msg;
	alarm = alarm->next;

	return (msg);
}

/**
 * Raise an alarm in the system.
 * @param type alarm error code
 * @param msg optional message string; a local copy is made
 * @param msglcd optional short message string, for LCD display; a local copy is made. No check on length, will be truncated on display if too long.
 * @return exec status
 */
int alarms_raise(const enum e_execs type, const char * const msg, const char * const msglcd)
{
	struct s_alarm * restrict alarm = NULL;

	if (!Alarms.online)
		return (-EOFFLINE);

	// create alarm
	alarm = calloc(1, sizeof(*alarm));
	if (!alarm)
		return (-EOOM);

	// populate structure
	alarm->type = type;
	if (msg)
		alarm->msg = strdup(msg);
	if (msglcd)
		alarm->msglcd = strdup(msglcd);

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
 * @return exec status
 */
int alarms_run(void)
{
	const char * msg;

	if (!Alarms.online)
		return (-EOFFLINE);

	while ((msg = alarms_msg_iterator(false)))
		pr_log("ALARM: %s", msg);

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