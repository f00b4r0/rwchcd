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

#ifdef DEBUG
	if (unlikely(dt < 1))
		dbgmsg("WARNING: rounding error. tau: %d, dt: %d", tau, dt);
#endif

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
 * Thus by design this function will lag the true derivative, especially for large #spread values.
 * @param deriv derivative data
 * @param new_temp new temperature point
 * @param new_time new temperature time
 * @param tau the strictly positive time to average over (averaging window)
 * @return a derivative value congruent to temp_t units / seconds (millikelvins / seconds).
 *
 * @note To reduce rounding errors, the function subsamples by an circa an order of magnitude when possible.
 * Furthermore, with default timekeep_t in deciseconds and temp_t in ~ millikelvins, the maximum resolution is
 * 0.001K/s (or 0.06K/mn, or 3.6K/h). The value of tau does not change the resolution.
 * @todo variable resolution
 */
temp_t temp_expw_deriv(struct s_temp_deriv * const deriv, const temp_t new_temp, const timekeep_t new_time, const timekeep_t tau)
{
	/* Assume largest tempdiff between two subsamples will be < INT16_MAX, i.e. c.64K max
	 * Use the extra 15bits as fixed point precision; store derivative internally with this format, which will
	 * provide sufficient precision to avoid propagating averaging errors.
	 * Final division (shift) will asymetrically truncate negative and positive numbers */
	const typeof(deriv->derivative) fpdec = (1<<15);
	temp_t tempdiff;
	timekeep_t timediff, tsample = (tau/8) ? tau/8 : tau;	// 8 is pow(2) approx of an order of magnitude

	assert(deriv);
	assert(tau > 0);

	deriv->inuse = true;

	if (unlikely(!deriv->last_time || !new_time))	// only compute derivative over a finite domain
		deriv->derivative = 0;
	else {
		tempdiff = new_temp - deriv->last_temp;
		timediff = new_time - deriv->last_time;

		// avoid divide-by-zero
		if (unlikely(!timediff))
			goto out;

		// subsample
		if (timediff < tsample)
			goto out;

		assert((tempdiff * fpdec) < INT32_MAX);
		assert((tempdiff * fpdec) > INT32_MIN);

		dbgmsg("tempdiff: %d, tempdiffmul: %d, timediff: %lld", tempdiff, tempdiff * fpdec, timediff);
		tempdiff *= fpdec;

		deriv->derivative = temp_expw_mavg(deriv->derivative, tempdiff / timekeep_tk_to_sec(timediff), tau, timediff);
	}

	deriv->last_time = new_time;
	deriv->last_temp = new_temp;

	dbgmsg("raw deriv: %d, deriv: %d", deriv->derivative, deriv->derivative/fpdec);
out:
	return (deriv->derivative/fpdec);
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
 * @return the integral value in temp_t units * seconds (millikelvins second)
 */
temp_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const timekeep_t new_time,
		      const temp_t tlow_jacket, const temp_t thigh_jacket)
{
	assert(intgrl);

	intgrl->inuse = true;

	if (unlikely(!intgrl->last_time || !new_time))	// only compute integral over a finite domain
		intgrl->integral = 0;
	else
		intgrl->integral += (((new_temp - thrsh) + (intgrl->last_temp - intgrl->last_thrsh))/2) * timekeep_tk_to_sec(new_time - intgrl->last_time);

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
