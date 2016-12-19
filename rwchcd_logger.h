//
//  rwchcd_logger.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Asynchronous timer API
 */

#ifndef rwchcd_logger_h
#define rwchcd_logger_h

#include <stdio.h>

/** List of logger callbacks */
struct s_logger_callback {
	time_t last_call;	///< last time the callback was called
	unsigned int period;	///< requested log period
	int (*cb)(void);	///< logger callback, must lock and call storage_log()
	struct s_logger_callback * next;
};

void * logger_thread(void * arg);
int logger_add_callback(unsigned int period, int (* cb)(void));
void logger_clean_callbacks(void);

#endif /* rwchcd_logger_h */
