//
//  timekeep.c
//  rwchcd
//
//  (C) 2019,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Timekeeping implementation.
 * This file implements a timekeeping thread that monotonically advances a tick counter to be used as a time reference within the program.
 * It also provides abstracted interfaces to sleeping routines for easy architecture-dependent implementation.
 */

#include <time.h>
#include <unistd.h>	// (u)sleep()
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>	// only for pthread_setname_np()
#include <errno.h>

#include "rwchcd.h"
#include "timekeep.h"

#define TIMEKEEP_RESNS	(1000000000L/TIMEKEEP_SMULT)	///< minimum clock source resolution required in nanoseconds.

static clockid_t TK_clockid;				///< after initialization, holds the correct clock id.
static struct timespec TK_tstart;			///< initial timestamp (set during initialization)
static _Atomic timekeep_t TK_wallclock;			///< internal wall clock updated once per loop. Assumes loop duration is dominated by delay and loop execution time is below wallclock resolution

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
 * Update the current timestamp.
 * This function atomically updates the internal wall clock.
 * @return exec status
 */
static int timekeep_clockupdate(void)
{
	static struct timespec tsnow;	// defined as static to reduce stack pressure since we can only be called from timekeep_thread()
	timekeep_t retval;
	time_t secdiff;
	long nsecdiff;

	if (clock_gettime(TK_clockid, &tsnow))
		return (-EGENERIC);

	secdiff = (tsnow.tv_sec - TK_tstart.tv_sec) * TIMEKEEP_SMULT;
	nsecdiff = (tsnow.tv_nsec - TK_tstart.tv_nsec) / TIMEKEEP_RESNS;

	retval = (unsigned)(secdiff + nsecdiff);

	// assert clock only goes forward
	assert(timekeep_a_ge_b(retval, aler(&TK_wallclock)));

	aser(&TK_wallclock, retval);

	return (ALL_OK);
}

/**
 * Sleep for at least N microseconds.
 * Signal-safe nanosleep() wrapper, handles EINTR internally.
 * @param usecs time to sleep for (in microseconds).
 * @note does not require timekeep_thread() to be running
 */
void timekeep_usleep(unsigned int usecs)
{
	struct timespec tv;
	int ret;

	tv.tv_sec = usecs / 1000000;
	tv.tv_nsec = (usecs % 1000000) * 1000;

	while (1) {
		ret = nanosleep(&tv, &tv);
		if (ret && (EINTR == errno))
			continue;
		return;
	}
}

/**
 * Sleep for at least N seconds.
 * @param seconds time to sleep for (in seconds).
 * @note does not require timekeep_thread() to be running
 */
void timekeep_sleep(unsigned int seconds)
{
	while ((seconds = sleep(seconds)));
}

/**
 * Get the current timestamp.
 * This function atomically reads the internal wall clock.
 * @return a monotonically growing timestamp value, with 0 being init time.
 * @warning this function uses a relaxed memory model, meaning that two concurrent calls may return different values.
 * This usually bears no consequence as long as minimal care is taken. For instance, known safe options are:
 * - this function is called only _once_ within a routine that sets and compares a value it has exclusive control over; or
 * - any time comparison between timestamps coming from different threads _also_ ensures that "time moved forward", by using e.g. timekeep_a_ge_b()
 * @note wraparound is not handled (should happen after a few centuries uptime).
 */
timekeep_t timekeep_now(void)
{
	return (aler(&TK_wallclock));
}

/**
 * Simple timekeep thread.
 * Update the wall clock at Nyquist frequency
 * @note hardcoded frequency
 */
void * timekeep_thread(void * arg __attribute__((unused)))
{
#ifdef _GNU_SOURCE
	pthread_setname_np(pthread_self(), "timekeep");
#endif

	// start logging
	while (1) {
		timekeep_clockupdate();
		timekeep_usleep(500*1000);
	}
}

