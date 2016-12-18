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

#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_storage.h"
#include "rwchcd_logger.h"

static struct s_logger_callback * Log_cb_head = NULL;

/**
 * Simple logger thread.
 * runs a busy loop through the callbacks every second.
 * @bug Will miss some logs if sleep() is too long due to the use of modulo.
 */
void * logger_thread(void * arg)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_logger_callback * lcb = NULL;
	static time_t min;
	time_t now;
	
	// wait for system to be configured
	while (!runtime->config || !runtime->config->configured)
		sleep(1);
	
	min = time(NULL);	// this makes a good max to start from
	
	// start logging
	while (Threads_master_sem) {
		now = time(NULL);
		
		for (lcb = Log_cb_head; lcb != NULL; lcb = lcb->next) {
			min = lcb->period < min ? lcb->period : min;	// find the minimum period
			
			if ((now % lcb->period))
				continue;
			
			if (lcb->cb())
				dbgerr("cb failed");
		}
		
		sleep(min);	// sleep for the shortest required log period
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
	
	lcb->period = period;
	lcb->cb = cb;
	lcb->next = Log_cb_head;
	
	Log_cb_head = lcb;
	
	return (ALL_OK);
}
