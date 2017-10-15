//
//  lib.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Basic API.
 */

#ifndef rwchcd_lib_h
#define rwchcd_lib_h

#include "rwchcd.h"

temp_t get_temp(const tempid_t id);
temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const time_t tau, const time_t dt);
temp_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const time_t new_time);

/**
 * Convert celsius value to internal temp_t format (Kelvin * KPRECISIONI).
 * @param celsius temp value in Celsius
 * @return value converted to internal type
 */
__attribute__((const, always_inline)) static inline temp_t celsius_to_temp(const float celsius)
{
	return ((temp_t)((celsius + 273.15F)*KPRECISIONI));
}

/**
 * Convert temperature from internal format to Celsius value.
 * @param temp temp value as temp_t
 * @return value converted to Celsius
 */
__attribute__((const, always_inline)) static inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/KPRECISIONF - 273.15F));
}

/**
 * Convert a temperature delta (in Kelvin) to internal type.
 * @param delta the delta value to be converted
 * @return the corresponding value expressed in internal temperature format.
 */
__attribute__((const, always_inline)) static inline temp_t deltaK_to_temp(const float delta)
{
	return ((temp_t)(delta * KPRECISIONI));
}

/** 
 * Convert delta from internal to Kelvin value.
 * @param temp the internal delta value to be converted
 * @return the value converted to Kelvin
 */
__attribute__((const, always_inline)) static inline float temp_to_deltaK(const temp_t temp)
{
	return ((float)((float)temp/KPRECISIONF));
}

/**
 * Calculate the minimum time interval to use with temp_expw_mavg() for a given
 * tau.
 * @param tau target tau
 * @return minimum usable time interval
 */
__attribute__((const, always_inline)) static inline time_t expw_mavg_dtmin(const time_t tau)
{
	return (/*ceilf*/(((1.0F/KPRECISIONF)*tau)/(1.0F-(1.0F/KPRECISIONF))) * 2);
}

/**
 * Validate a temperature value
 * @param temp the value to validate
 * @return validation result
 */
__attribute__((const, always_inline)) static inline int validate_temp(const temp_t temp)
{
	int ret = ALL_OK;

	switch (temp) {
		case TEMPUNSET:
			ret = -ESENSORINVAL;
			break;
		case TEMPSHORT:
			ret = -ESENSORSHORT;
			break;
		case TEMPDISCON:
			ret = -ESENSORDISCON;
			break;
		default:
			break;
	}

	return (ret);
}

#endif /* rwchcd_lib_h */
