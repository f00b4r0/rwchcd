//
//  lib.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Basic functions used throughout the program.
 */

#include <math.h>	// roundf

#include "rwchcd.h"
#include "lib.h"

/**
 * Exponentially weighted moving average implementing a trivial LP filter.
 * - http://www.rowetel.com/?p=1245
 * - https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 * - http://www.edn.com/design/systems-design/4320010/A-simple-software-lowpass-filter-suits-embedded-system-applications
 * @warning if dt is 0 then the value will never be updated (dt has a 1s resolution)
 * @param filtered accumulated average
 * @param new_sample new sample to average
 * @param tau time constant over which to average
 * @param dt time elapsed since last average
 */
__attribute__((const)) temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const time_t tau, const time_t dt)
{
	float alpha = (float)dt / (tau+dt);	// dt sampling itvl, tau = constante de temps
	
	if (alpha <= 1.0F/KPRECISIONF)
		dbgerr("WARNING: rounding error. tau: %ld, dt: %ld", tau, dt);
	
	return (filtered - roundf(alpha * (filtered - new_sample)));
}

/**
 * Jacketed threshold temperature integral, trapezoidal method.
 * This function calculates the integral over time of a temperature series after
 * subtracting a threshold value: the integral is positive if the values are above
 * the threshold, and negative otherwise.
 * The trapezoidal method is used here as it's the easiest to implement in a
 * discrete context.
 * This function applies upper and lower bounds to the integral for jacketing.
 * @note by design this method will underestimate the integral value.
 * @param intgrl integral data
 * @param thrsh threshold value
 * @param new_temp new temperature point
 * @param new_time new temperature time
 * @param tlow_jacket low boundary for integral jacket
 * @param thigh_jacket high boundary for integral jacket
 * @return the integral value in temp_t units * time_t units (millikelvins second)
 */
temp_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const time_t new_time,
		      const temp_t tlow_jacket, const temp_t thigh_jacket)
{
	if ((0 == intgrl->last_time) || (thrsh != intgrl->last_thrsh))	// reset condition
		intgrl->integral = 0;
	else
		intgrl->integral += (((new_temp + intgrl->last_temp)/2) - thrsh) * (new_time - intgrl->last_time);

	// apply jackets
	if (intgrl->integral < tlow_jacket)
		intgrl->integral = tlow_jacket;
	else if (intgrl->integral > thigh_jacket)
		intgrl->integral = thigh_jacket;

	intgrl->last_thrsh = thrsh;
	intgrl->last_time = new_time;
	intgrl->last_temp = new_temp;

	return (intgrl->integral);
}
