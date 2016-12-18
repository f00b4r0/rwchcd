//
//  rwchcd_logger.h
//  rwchcd
//
//  Created by Thibaut VARÈNE on 18/12/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

#ifndef rwchcd_logger_h
#define rwchcd_logger_h

#include <stdio.h>

/** List of logger callbacks */
struct s_logger_callback {
	time_t last_call;	///< last time the callback was called
	unsigned int period;	///< requested log period
	int (*cb)(void);	///< logger callback, must lock and call storage_log()
	struct s_logger_callback * restrict next;
};

void * logger_thread(void * arg);
int logger_add_callback(unsigned int period, int (* cb)(void));
void logger_clean_callbacks(void);

#endif /* rwchcd_logger_h */
