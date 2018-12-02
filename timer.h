//
//  timer.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Asynchronous timer API
 */

#ifndef rwchcd_timer_h
#define rwchcd_timer_h

typedef int (*timer_cb_t)(void);

void * timer_thread(void * arg);
int timer_add_cb(unsigned int period, timer_cb_t, const char * const name);
void timer_clean_callbacks(void);

#endif /* rwchcd_timer_h */
