//
//  lib.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Basic functions used throughout the program.
 */

#include <assert.h>
#include <limits.h>	// CHAR_BIT

#include "lib.h"

// NB: we rely on the fact that gcc sign-extends
#define sign(x)		((x>>(sizeof(x)*CHAR_BIT-1))|1)		///< -1 if x<0, 1 if x>=0
#define zerosign(x)	((x>>(sizeof(x)*CHAR_BIT-1))|(!!x))	///< -1 if x<0, 1 if x>0, 0 if x==0

/**
 * Exponentially weighted moving average implementing a trivial LP filter.
 * - http://www.rowetel.com/?p=1245
 * - https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 * - http://www.edn.com/design/systems-design/4320010/A-simple-software-lowpass-filter-suits-embedded-system-applications
 *
 * Formula:
 * - alpha = dt/(tau+dt)
 * - return value = (filtered - roundf(alpha * (filtered - new_sample)))
 *
 * @warning if dt is 0 then the value will never be updated (dt has a 1s resolution)
 * @warning if dt and (filtered - new_sample) are both large the computation may overflow
 * @note this LP filter could be implemented more efficiently if the assumption of a fixed sampling interval is made. See provided URLs.
 * @param filtered accumulated average
 * @param new_sample new sample to average
 * @param tau time constant over which to average
 * @param dt time elapsed since last average
 * @return the computed average
 */
__attribute__((const)) temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const timekeep_t tau, const timekeep_t dt)
{
	const temp_t tdiff = (filtered - new_sample);
	const timekeep_t tdt = (tau + dt);

	assert(tdt);

	dbgmsg(2, (unlikely(dt < 1)), "WARNING: rounding error. tau: %d, dt: %d", tau, dt);

	// assert (dt << TEMPT_MAX), assert (tdt << TEMPT_MAX)
	return (filtered - (((signed)dt * tdiff + sign(tdiff)*(signed)(tdt)/2) / (signed)(tdt)));
	//                                 ^-- this rounds
}

/**
 * Exponentially weighted derivative.
 * Computes an exponentially weighted discrete derivative in the time domain.
 * This function computes a discrete derivative by subtracting the new sample value with the last sample
 * value over the total time elapsed between the two samples, and then averages this result over
 * #spread samples by using temp_expw_mavg().
 * Thus by design this function will lag the true derivative, especially for large #tau values.
 * @param deriv derivative data
 * @param new_temp new temperature point
 * @param new_time new temperature time
 * @param tau the strictly positive time to average over (averaging window). Must be >= 8.
 * @return a scaled derivative value congruent to temp_t units / timekeep_t units.
 *
 * @warning the output is scaled. Multiplication and division should use the correct accessors.
 * @note To reduce rounding errors, the function subsamples 4 times.
 */
temp_t temp_expw_deriv(struct s_temp_deriv * const deriv, const temp_t new_temp, const timekeep_t new_time, const timekeep_t tau)
{
	const timekeep_t tsample = tau/4;	 // subsampling ratio
	timekeep_t timediff;
	temp_t tempdiff, drv;

	assert(deriv);
	assert((tau >= 8) && (tau < INT32_MAX));

	drv = deriv->derivative;

	if (unlikely(!deriv->last_time))	// only compute derivative over a finite domain
		drv = 0;
	else {
		assert(timekeep_a_ge_b(new_time, deriv->last_time));

		timediff = new_time - deriv->last_time;

		// avoid divide-by-zero
		if (unlikely(!timediff))
			goto out;

		// subsample
		if (timediff < tsample)
			goto out;

		tempdiff = new_temp - deriv->last_temp;
		tempdiff *= LIB_DERIV_FPDEC;

		drv = temp_expw_mavg(drv, tempdiff / (signed)timediff, tau, timediff);
		dbgmsg(2, 1, "raw deriv: %d, tempdiff: %d, timediff: %d, tau: %d", drv, tempdiff, timediff, tau);
	}

	deriv->derivative = drv;
	deriv->last_temp = new_temp;
	deriv->last_time = new_time;

out:
	return (drv);
}

/**
 * Jacketed threshold temperature integral, trapezoidal method.
 * This function calculates the integral over time of a temperature series after
 * subtracting a threshold value: the integral is positive if the values are above
 * the threshold, and negative otherwise.
 * The trapezoidal method is used here as it's the easiest to implement in a
 * discrete context.
 * This function applies upper and lower bounds to the integral for jacketing.
 * The computation accepts a moving threshold, the provided threshold will only
 * be applied to the provided new value and the previous threshold will be used
 * for the previous value in the calculation.
 * @note by design this method will underestimate the integral value.
 * @param intgrl integral data
 * @param thrsh threshold value
 * @param new_temp new temperature point
 * @param new_time new temperature time
 * @param tlow_jacket low boundary for integral jacket
 * @param thigh_jacket high boundary for integral jacket
 * @return the integral value in temp_t units * timekeep_t units
 */
temp_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const timekeep_t new_time,
		      const temp_t tlow_jacket, const temp_t thigh_jacket)
{
	temp_t intg;

	assert(intgrl);
	assert(timekeep_a_ge_b(new_time, intgrl->last_time));

	intgrl->inuse = true;

	intg = intgrl->integral;

	if (unlikely(!intgrl->last_time || !new_time))	// only compute integral over a finite domain
		intg = 0;
	else
		intg += (((new_temp - thrsh) + (intgrl->last_temp - intgrl->last_thrsh))/2) * (new_time - intgrl->last_time);

	// apply jackets
	if (intg < tlow_jacket)
		intg = tlow_jacket;
	else if (intg > thigh_jacket)
		intg = thigh_jacket;

	intgrl->integral = intg;
	intgrl->last_thrsh = thrsh;
	intgrl->last_time = new_time;
	intgrl->last_temp = new_temp;

	return (intg);
}
