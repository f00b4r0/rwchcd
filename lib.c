//
//  lib.c
//  rwchcd
//
//  (C) 2016-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Basic functions used throughout the program.
 */

#include <assert.h>

#include "lib.h"

/**
 * Convert temperature from internal format to Celsius value.
 * @note Ensure this function is only used in non-fast code path (dbgmsg, config handling...).
 * @param temp temp value as temp_t
 * @return value converted to Celsius
 */
__attribute__((const)) float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/KPRECISION - 273));
}

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
	const tempdiff_t tdiff = (tempdiff_t)(filtered - new_sample);
	const timekeep_t tdt = (tau + dt);

	assert(tdt);

	dbgmsg(3, (unlikely(dt < 1)), "WARNING: possible rounding error. tau: %d, dt: %d", tau, dt);

	// assert (dt << TEMPT_MAX), assert (tdt << TEMPT_MAX)
	return (filtered - ((signed)(dt * tdiff + sign(tdiff)*((tdt)/2)) / (signed)(tdt)));
	//                                 ^-- this rounds
}

/**
 * Compute a linear discrete derivative in the time domain.
 * This function computes a discrete derivative by subtracting the new sample value with the last sample
 * value over the total time elapsed between the two samples.
 * @param deriv derivative data
 * @param new_temp new temperature point
 * @param new_time new temperature time
 * @param tau the strictly positive time to sample over.
 * @return a scaled derivative value congruent to temp_t units / timekeep_t units.
 *
 * @warning the output is scaled. Multiplication and division should use the correct accessors.
 */
tempdiff_t temp_lin_deriv(struct s_temp_deriv * const deriv, const temp_t new_temp, const timekeep_t new_time, const timekeep_t tau)
{
	timekeep_t timediff;
	tempdiff_t tempdiff, drv;

	assert(deriv);
	assert(tau < INT32_MAX);

	drv = deriv->derivative;

	if (unlikely(!deriv->last_time))	// only compute derivative over a finite domain
		drv = 0;
	else {
		assert(timekeep_a_ge_b(new_time, deriv->last_time));

		timediff = new_time - deriv->last_time;

		// avoid divide-by-zero
		if (unlikely(!timediff))
			goto out;

		// wait
		if (timediff < tau)
			goto out;

		tempdiff = (tempdiff_t)(new_temp - deriv->last_temp);
		tempdiff *= LIB_DERIV_FPDEC;

		drv = tempdiff / (signed)timediff;
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
tempdiff_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const timekeep_t new_time,
		      const tempdiff_t tlow_jacket, const tempdiff_t thigh_jacket)
{
	tempdiff_t intg;

	assert(intgrl);
	assert(timekeep_a_ge_b(new_time, intgrl->last_time));

	intg = intgrl->integral;

	if (unlikely(!intgrl->last_time || !new_time))	// only compute integral over a finite domain
		intg = 0;
	else
		intg += (((tempdiff_t)((new_temp - thrsh) + (intgrl->last_temp - intgrl->last_thrsh))/2) * (signed)(new_time - intgrl->last_time));

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

/**
 * Compares old and new runmode for change down detection.
 * @param prev_runmode the old runmode
 * @param new_runmode the new runmode
 * @return true if new_runmode is different and "lower" than prev_runmode; false in all other cases.
 * @note: only valid for RM_OFF, RM_COMFORT, RM_ECO and RM_FROSTFREE.
 */
bool lib_runmode_is_changedown(const enum e_runmode prev_runmode, const enum e_runmode new_runmode)
{
	bool down = false;

	if (prev_runmode == new_runmode)
		return false;

	switch (new_runmode) {
		case RM_OFF:
			down = true;	// always true
			break;
		case RM_COMFORT:
			break;	// always false
		case RM_ECO:
		case RM_FROSTFREE:
			if ((RM_COMFORT == prev_runmode) || (RM_ECO == prev_runmode))
				down = true;
			break;
		case RM_AUTO:
		case RM_TEST:
		case RM_UNKNOWN:
		case RM_SUMMAINT:
		case RM_DHWONLY:
		default:
			dbgerr("Invalid comparison! (%d, %d)", prev_runmode, new_runmode);
			break;
	}

	return (down);
}

