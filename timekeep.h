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

#define TIMEKEEP_SMULT	10		///< second multiplier: 10 -> 0.1s precision

/**
 * Timestamp 'tick' type.
 * Timestamp precision is 0.1s (see #TIMEKEEP_SMULT). We use uint32_t:
 *  - Counter will wrap after UINT32_MAX/TIMEKEEP_SMULT seconds. With 0.1s precision, wraparound occurs after 2485 days or approx 6.8 years
 *  - We assume that we will never need to measure time differences larger than wraparound period/2, or approx 3.4 years.
 * Under these assumptions, unsigned integer arithmetics will work very nicely and will be fast on all platforms
 */
typedef uint32_t	timekeep_t;
#define TIMEKEEP_MAX	UINT32_MAX

int timekeep_init(void);
void timekeep_exit(void);
timekeep_t timekeep_now(void);
void timekeep_usleep(unsigned int usecs);
void timekeep_sleep(unsigned int seconds);
void * timekeep_thread(void * arg);

/**
 * Sleep for at least N seconds.
 * @param seconds time to sleep for (in seconds).
 * @warning Max 4294 seconds.
 * @note does not require timekeep_thread() to be running
 */
#define timekeep_sleep(seconds)		timekeep_usleep((seconds) * 1000000)

/**
 * Convert seconds to timekeep_t format.
 * @param seconds value to convert.
 * @return the value correctly formatted.
 * @warning seconds must be < UINT32_MAX/TIMEKEEP_SMULT
 */
#define timekeep_sec_to_tk(seconds)	(timekeep_t)(seconds * TIMEKEEP_SMULT)

/**
 * Convert timekeep_t format back to seconds.
 * @param tk value to convert.
 * @return the value expressed in seconds.
 */
#define timekeep_tk_to_sec(tk)		(tk / TIMEKEEP_SMULT)

/**
 * Tests if timestamp a is after or at timestamp b.
 * @param a timekeep_t timestamp
 * @param b timekeep_t timestamp
 * @return false is b is strictly after a, true otherwise (including a == b)
 */
#define timekeep_a_ge_b(a, b)		((a - b) < TIMEKEEP_MAX/2)

#endif /* timekeep_h */
