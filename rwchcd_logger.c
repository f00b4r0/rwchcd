//
//  rwchcd_logger.c
//  rwchcd
//
//  Created by Thibaut VARÈNE on 18/12/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

#include <unistd.h>	// sleep/usleep/setuid
#include <stdlib.h>	// calloc
#include <assert.h>
#include <pthread.h>	// rwlock

#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_storage.h"
#include "rwchcd_logger.h"

static struct s_logger_callback * Log_cb_head = NULL;
static unsigned int Log_period_min = 0;
static pthread_rwlock_t Log_rwlock = PTHREAD_RWLOCK_INITIALIZER;

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
		
		pthread_rwlock_rdlock(&Log_rwlock);
		for (lcb = Log_cb_head; lcb != NULL; lcb = lcb->next) {
			if ((now - lcb->last_call) < lcb->period)
				continue;
			
			if (lcb->cb())
				dbgerr("cb failed");
			
			lcb->last_call = now;	// only touched here, the way we lock is fine
		}
		pthread_rwlock_unlock(&Log_rwlock);
		
		sleep(Log_period_min);	// sleep for the shortest required log period - XXX TODO: pb if later added cbs have shorter period that the one currently sleeping on. Use select() and a pipe?
	}
}

/**
 * Add a logger callback.
 * @param period the period at which that callback should be called
 * @param cb the callback function to call
 * @return exec status
 */
int logger_add_callback(unsigned int period, int (* cb)(void))
{
	struct s_logger_callback * lcb = NULL;
	
	if ((period < 1) || (!cb))
		return (-EINVALID);
	
	lcb = calloc(1, sizeof(struct s_logger_callback));
	if (!lcb)
		return (-EOOM);
	
	pthread_rwlock_wrlock(&Log_rwlock);
	if (!Log_period_min)
		Log_period_min = period;
	else
		Log_period_min = period < Log_period_min ? period : Log_period_min;	// find the minimum period
	
	lcb->period = period;
	lcb->cb = cb;
	lcb->next = Log_cb_head;	// this could cause a loop if we didn't lock
	Log_cb_head = lcb;
	pthread_rwlock_unlock(&Log_rwlock);
	
	return (ALL_OK);
}

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
