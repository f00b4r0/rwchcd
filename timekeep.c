//
//  timekeep.c
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Timekeeping implementation.
 * @todo possibly switch to int32 instead of int64 and handle wraparound after ~6years (when using deciseconds as base unit)
 */

#include <time.h>
#include <unistd.h>	// sleep()

#include "rwchcd.h"
#include "timekeep.h"

#define TIMEKEEP_RESNS	(1000000000L/TIMEKEEP_SMULT)	///< minimum clock source resolution required in nanoseconds.

static clockid_t TK_clockid;				///< after initialization, holds the correct clock id.
static struct timespec TK_tstart;			///< initial timestamp (set during initialization)

/**
 * Init timekeeping subsystem.
 * This function tries various monotonic clock sources and will fail
 * if none is available.
 * @return exec status
 */
int timekeep_init(void)
{
	struct timespec res;
	int ret;

#ifdef CLOCK_MONOTONIC_COARSE
	// test coarse
	TK_clockid = CLOCK_MONOTONIC_COARSE;
	ret = clock_getres(TK_clockid, &res);
	if (!ret) {
		if (!res.tv_sec && (TIMEKEEP_RESNS >= res.tv_nsec)) {	// all good
			clock_gettime(TK_clockid, &TK_tstart);
			return (ALL_OK);
		}
	}
#endif

	// didnt work or not available, test regular
	TK_clockid = CLOCK_MONOTONIC;
	ret = clock_getres(TK_clockid, &res);
	if (!ret) {
		if (!res.tv_sec && (TIMEKEEP_RESNS > res.tv_nsec)) {	// all good
			clock_gettime(TK_clockid, &TK_tstart);
			return (ALL_OK);
		}
	}

	// nothing good so far, fail
	return (-EINIT);
}

/**
 * Exit timekeeping subsystem.
 * (currently this function is a NOOP)
 */
void timekeep_exit()
{
}

/**
 * Get the current timestamp.
 * @return a monotonically growing timestamp value, with 0 being init time.
 * @note wraparound is not handled (should happen after a few centuries uptime).
 */
timekeep_t timekeep_now(void)
{
	struct timespec tsnow;
	timekeep_t retval;

	if (clock_gettime(TK_clockid, &tsnow))
		return (-1);

	retval = (tsnow.tv_sec - TK_tstart.tv_sec) * TIMEKEEP_SMULT;
	retval += (tsnow.tv_nsec - TK_tstart.tv_nsec) / TIMEKEEP_RESNS;

	return (retval);
}

/**
 * Sleep for at least N seconds.
 * @param seconds time to sleep for.
 */
void timekeep_sleep(unsigned int seconds)
{
	while ((seconds = sleep(seconds)));
}
