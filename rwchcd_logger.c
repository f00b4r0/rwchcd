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
static time_t Log_period_min = 0;
static pthread_rwlock_t Log_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * Simple logger thread.
 * runs a busy loop through the callbacks every second.
 * @bug Will miss some logs if sleep() is too long due to the use of modulo.
 */
void * logger_thread(void * arg)
{
	struct s_logger_callback * lcb = NULL;
	time_t now;
	
	// wait for first callback to be configured
	while (!Log_cb_head)	// lockless due to proper ordering in logger_add_callback()
		sleep(1);
	
	// start logging
	while (Threads_master_sem) {
		now = time(NULL);
		
		pthread_rwlock_rdlock(&Log_rwlock);
		for (lcb = Log_cb_head; lcb != NULL; lcb = lcb->next) {
			if ((now % lcb->period))
				continue;
			
			if (lcb->cb())
				dbgerr("cb failed");
		}
		pthread_rwlock_unlock(&Log_rwlock);
		
		sleep(Log_period_min);	// sleep for the shortest required log period
	}
	
	pthread_exit(NULL);
}

/**
 * Add a logger callback.
 * @param period the period at which that callback should be called
 * @param cb the callback function to call
 * @return exec status
 */
int logger_add_callback(time_t period, int (* cb)(void))
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
