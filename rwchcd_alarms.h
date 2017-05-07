//
//  rwchcd_alarms.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Alarms implementation API.
 */

#ifndef rwchcd_alarms_h
#define rwchcd_alarms_h

int alarms_online(void);
int alarms_count(void);
const char * alarms_msg_iterator(const bool msglcd);
int alarms_raise(const enum e_execs type, const char * const msg, const char * const msglcd);
int alarms_run(void);
void alarms_offline(void);

#endif /* rwchcd_alarms_h */
