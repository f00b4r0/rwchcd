//
//  timer.c
//  rwchcd
//
//  (C) 2016,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Asynchronous timer operations
 */

#include <unistd.h>	// sleep/usleep/setuid
#include <stdlib.h>	// calloc
#include <string.h>	// strdup
#include <stdatomic.h>

#include "rwchcd.h"
#include "timer.h"
#include "timekeep.h"

/** timer callbacks */
struct s_timer_cb {
	timekeep_t last_call;	///< last time the callback was called
	timekeep_t tkperiod;	///< requested timer period
	int (*cb)(void);	///< timed callback, must lock where necessary
	char * name;		///< callback name
	struct s_timer_cb * next;	///< pointer to next callback
};

static struct s_timer_cb * Timer_cb_head = NULL;	///< list of timer callbacks
static unsigned int Timer_period_min = 0;	///< time between runs in seconds
static atomic_flag Timer_cb_flag = ATOMIC_FLAG_INIT;	///< list modification protection flag

/**
 * Simple timer thread.
 * runs a delay loop through the callbacks.
 * Callbacks are called sequentially one after the other: there is no concurrency.
 * @todo improve imperfect time handling.
 */
void * timer_thread(void * arg __attribute__((unused)))
{
	struct s_timer_cb * lcb;
	unsigned int tperiod;
	timekeep_t now;
	int ret;

	// note: must NOT sleep while holding the flag
	while (1) {
		while (unlikely(atomic_flag_test_and_set_explicit(&Timer_cb_flag, memory_order_acquire)));	// cannot yield here

		tperiod = Timer_period_min;

		// wait for first callback to be configured
		if (unlikely(!tperiod)) {
			atomic_flag_clear_explicit(&Timer_cb_flag, memory_order_release);
			timekeep_sleep(10);
			continue;
		}

		now = timekeep_now();

		for (lcb = Timer_cb_head; lcb != NULL; lcb = lcb->next) {
			if ((now - lcb->last_call) < lcb->tkperiod)
				break;	// ordered list, first mismatch means we don't need to check further
			
			ret = lcb->cb();
			if (unlikely(ALL_OK != ret))
				pr_log("Timer callback failed: \"%s\" (%d)", lcb->name, ret);

			lcb->last_call = now;	// only updated here
		}

		atomic_flag_clear_explicit(&Timer_cb_flag, memory_order_release);

		timekeep_sleep(tperiod);	// sleep for the shortest required log period - XXX TODO: pb if later added cbs have shorter period that the one currently sleeping on. Use select() and a pipe?
	}
}

/**
 * Basic GCD non-recursive implementation
 * @param a first number
 * @param b second number
 * @return GCD of a and b
 */
static inline unsigned int ugcd(unsigned int a, unsigned int b)
{
	unsigned int c;
	
	while (a) {
		c = a;
		a = b % a;
		b = c;
	}
	
	return b;
}

/**
 * Add a timer callback.
 * Insert callback ordered (by ascending period) in the callback list.
 * @param period the period (seconds) at which that callback should be called
 * @param cb the callback function to call
 * @param name a user-defined name for the timer
 * @return exec status
 * @note implements spinlocks
 */
int timer_add_cb(unsigned int period, int (* cb)(void), const char * const name)
{
	struct s_timer_cb * lcb = NULL, * lcb_before, * lcb_after;
	timekeep_t tkperiod;
	char * str = NULL;
	
	if ((period < 1) || (!cb))
		return (-EINVALID);
	
	lcb = calloc(1, sizeof(*lcb));
	if (!lcb)
		return (-EOOM);

	if (name) {
		str = strdup(name);
		if (!str) {
			free(lcb);
			return (-EOOM);
		}
	}

	tkperiod = timekeep_sec_to_tk(period);
	lcb_before = NULL;

	// critical section begins - spin lock
	while (atomic_flag_test_and_set_explicit(&Timer_cb_flag, memory_order_acquire))
		usleep(500);	// yield

	lcb_after = Timer_cb_head;
	
	// find insertion place
	while (lcb_after) {
		if (lcb_after->tkperiod > tkperiod)
			break;
		
		lcb_before = lcb_after;
		lcb_after = lcb_before->next;
	}

	lcb->name = str;
	lcb->cb = cb;
	lcb->tkperiod = tkperiod;
	
	/* Begin fence section.
	 * XXX REVISIT memory order is important here for this code to work reliably
	 * lockless. We probably need a fence. This is not "mission critical" so
	 * I'll leave it as is for now. */
	lcb->next = lcb_after;
	
	if (!lcb_before)
		Timer_cb_head = lcb;
	else
		lcb_before->next = lcb;
	/* End fence section */
	
	if (!Timer_period_min)
		Timer_period_min = period;
	else
		Timer_period_min = ugcd(period, Timer_period_min);	// find the GCD period

	// critical section ends - unlock
	atomic_flag_clear_explicit(&Timer_cb_flag, memory_order_release);
	
	dbgmsg("name: \"%s\", period: %u, new_min: %u", name, period, Timer_period_min);
	
	return (ALL_OK);
}

/**
 * Cleanup callback list.
 * @note implements spinlocks
 */
void timer_clean_callbacks(void)
{
	struct s_timer_cb * lcb, * lcbn;

	while (atomic_flag_test_and_set_explicit(&Timer_cb_flag, memory_order_acquire))
		usleep(500);	// yield

	lcb = Timer_cb_head;
	Timer_cb_head = NULL;
	Timer_period_min = 0;

	atomic_flag_clear_explicit(&Timer_cb_flag, memory_order_release);

	while (lcb) {
		lcbn = lcb->next;
		if (lcb->name)
			free(lcb->name);
		free(lcb);
		lcb = lcbn;
	}
}
