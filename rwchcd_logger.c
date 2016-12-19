//
//  rwchcd_logger.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Asynchronous timer operations
 */

#include <unistd.h>	// sleep/usleep/setuid
#include <stdlib.h>	// calloc
#include <assert.h>

#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_storage.h"
#include "rwchcd_logger.h"

static struct s_logger_callback * Log_cb_head = NULL;
static volatile unsigned int Log_period_min = 0;

/**
 * Simple logger thread.
 * runs a busy loop through the callbacks every second.
 * @bug buggy time handling.
 */
void * logger_thread(void * arg)
{
	struct s_logger_callback * lcb;
	time_t now;
	
	// wait for first callback to be configured
	while (!Log_period_min)	// XXX lockless
		sleep(1);
	
	// start logging
	while (1) {
		now = time(NULL);
		
		for (lcb = Log_cb_head; lcb != NULL; lcb = lcb->next) {
			if ((now - lcb->last_call) < lcb->period)
				break;	// ordered list, first mismatch means we don't need to check further
			
			if (lcb->cb) {	// avoid segfault in case for some reason the pointer isn't (yet) valid (due to e.g. memory reordering)
				if (lcb->cb())
					dbgerr("cb failed");
	
				lcb->last_call = now;	// only touched here, the way we lock is fine
			}
		}
		
		sleep(Log_period_min);	// sleep for the shortest required log period - XXX TODO: pb if later added cbs have shorter period that the one currently sleeping on. Use select() and a pipe?
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
 * Add a logger callback.
 * Insert callback ordered (by ascending period) in the callback list.
 * @param period the period at which that callback should be called
 * @param cb the callback function to call
 * @return exec status
 */
int logger_add_callback(unsigned int period, int (* cb)(void))
{
	struct s_logger_callback * lcb = NULL, * lcb_before, * lcb_after;
	
	if ((period < 1) || (!cb))
		return (-EINVALID);
	
	lcb = calloc(1, sizeof(struct s_logger_callback));
	if (!lcb)
		return (-EOOM);

	lcb_after = lcb_before = Log_cb_head;
	
	// find insertion place
	while (lcb_after) {
		if (lcb_after->period > period)
			break;
		
		lcb_before = lcb_after;
		lcb_after = lcb_before->next;
	}

	lcb->cb = cb;
	lcb->period = period;
	
	/* Begin fence section.
	 * XXX REVISIT memory order is important here for this code to work reliably
	 * lockless. We probably need a fence. This is not "mission critical" so
	 * I'll leave it as is for now. */
	lcb->next = lcb_after;
	
	if (lcb_before == Log_cb_head)
		Log_cb_head = lcb;
	else
		lcb_before->next = lcb;
	/* End fence section */
	
	if (!Log_period_min)
		Log_period_min = period;
	else
		Log_period_min = ugcd(period, Log_period_min);	// find the GCD period
	
	dbgmsg("period: %u, new_min: %u", period, Log_period_min);
	
	return (ALL_OK);
}

/**
 * Cleanup callback list.
 * @warning @b LOCKLESS This function must only be called when neither logger_thread()
 * nor logger_add_callback() are running or can run.
 */
void logger_clean_callbacks(void)
{
	struct s_logger_callback * lcb, * lcbn;

	lcb = Log_cb_head;
	while (lcb) {
		lcbn = lcb->next;
		free(lcb);
		lcb = lcbn;
	}
}
