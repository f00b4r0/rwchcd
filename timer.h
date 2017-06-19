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

void * timer_thread(void * arg);
int timer_add_cb(unsigned int period, int (* cb)(void));
void timer_clean_callbacks(void);

#endif /* rwchcd_timer_h */
