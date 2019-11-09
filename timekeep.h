//
//  timekeep.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Timekeeping API.
 */

#ifndef timekeep_h
#define timekeep_h

#include <inttypes.h>

#define TIMEKEEP_SMULT	10L				///< second multiplier: 10 -> 0.1s precision

/** Timestamp 'tick' type. Timestamp precision is 0.1s (see #TIMEKEEP_SMULT) */
typedef int64_t	timekeep_t;

int timekeep_init(void);
void timekeep_exit(void);
timekeep_t timekeep_now(void);
void timekeep_sleep(unsigned int seconds);

/**
 * Convert seconds to timekeep_t format.
 * @param seconds value to convert.
 * @return the value correctly formatted.
 */
__attribute__((const, always_inline)) static inline timekeep_t timekeep_sec_to_tk(long long seconds)
{
	return (seconds * TIMEKEEP_SMULT);
}


/**
 * Convert timekeep_t format back to seconds.
 * @param tk value to convert.
 * @return the value expressed in seconds.
 */
__attribute__((const, always_inline)) static inline long long timekeep_tk_to_sec(timekeep_t tk)
{
	return (tk / TIMEKEEP_SMULT);
}

#endif /* timekeep_h */
