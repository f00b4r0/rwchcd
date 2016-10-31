//
//  rwchcd_lib.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include <math.h>	// roundf

#include "rwchcd.h"
#include "rwchcd_runtime.h"

/**
 * Validate a temperature value
 * @param temp the value to validate
 * @return validation result
 */
int validate_temp(const temp_t temp)
{
	int ret = ALL_OK;

	if (temp == 0)
		ret = -ESENSORINVAL;
	else if (temp <= RWCHCD_TEMPMIN)
		ret = -ESENSORSHORT;
	else if (temp >= RWCHCD_TEMPMAX)
		ret = -ESENSORDISCON;

	return (ret);
}


/**
 * get temp from a given temp id
 * @param the physical id (counted from 1) of the sensor
 * @return temp if id valid, 0 otherwise
 * @warning no param check
 */
temp_t get_temp(const tempid_t id)
{
	const struct s_runtime * const runtime = get_runtime();

	if ((id <= 0) || (id > runtime->config->nsensors))
		return (0);

	return (runtime->temps[id-1]);	// XXX REVISIT lock
}

/**
 * Exponentially weighted moving average implementing a trivial LP filter
 http://www.rowetel.com/?p=1245
 https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 http://www.edn.com/design/systems-design/4320010/A-simple-software-lowpass-filter-suits-embedded-system-applications
 * @warning if dt is 0 then the value will never be updated (dt has a 1s resolution)
 * @param filtered accumulated average
 * @param new_sample new sample to average
 * @param tau time constant over which to average
 * @param dt time elapsed since last average
 */
temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const time_t tau, const time_t dt)
{
	float alpha = (float)dt / (tau+dt);	// dt sampling itvl, tau = constante de temps
	
	return (filtered - roundf(alpha * (filtered - new_sample)));
}