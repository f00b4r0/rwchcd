//
//  lib.h
//  rwchcd
//
//  (C) 2016-2017,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Basic functions used throughout the program, API.
 */

#ifndef rwchcd_lib_h
#define rwchcd_lib_h

#include <string.h>	// memset
#include <assert.h>

#include "rwchcd.h"
#include "timekeep.h"

/** Temperature integral data */
struct s_temp_intgrl {
	bool inuse;			///< true if integral is in use
	temp_t integral;		///< integral value in temp_t * seconds
	temp_t last_thrsh;		///< temperature threshold for integral calculation
	temp_t last_temp;		///< last recorded temperature value
	timekeep_t last_time;		///< last recorded temperature time
};

temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const timekeep_t tau, const timekeep_t dt);
temp_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const timekeep_t new_time,
		      const temp_t tlow_jacket, const temp_t thigh_jacket);

/**
 * Convert celsius value to internal temp_t format (Kelvin * KPRECISIONI).
 * @note The preprocessor will do the right thing whether celsius is a float or a native integer type.
 * @param celsius temp value in Celsius
 */
#define celsius_to_temp(celsius)	(temp_t)((celsius + 273)*KPRECISIONI)

/**
 * Convert temperature from internal format to Celsius value.
 * @note Ensure this function is only used in non-fast code path (dbgmsg, config handling...).
 * @param temp temp value as temp_t
 * @return value converted to Celsius
 */
__attribute__((const, always_inline)) static inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/KPRECISIONI - 273));
}

/**
 * Convert a temperature delta (in Kelvin) to internal type.
 * @note The preprocessor will do the right thing whether delta is a float or a native integer type.
 * @param delta the delta value to be converted
 */
#define deltaK_to_temp(delta)		(temp_t)(delta * KPRECISIONI)

/** 
 * Convert delta from internal to Kelvin value.
 * @note Ensure this function is only used in non-fast code path (dbgmsg, config handling...).
 * @param temp the internal delta value to be converted
 * @return the value converted to Kelvin
 */
__attribute__((const, always_inline)) static inline float temp_to_deltaK(const temp_t temp)
{
	return ((float)((float)temp/KPRECISIONI));
}

/**
 * Calculate the minimum time interval to use with temp_expw_mavg() for a given
 * tau.
 * This function 'ceils' the return value.
 * @param tau target tau
 * @return minimum usable time interval
 */
__attribute__((const, always_inline)) static inline timekeep_t expw_mavg_dtmin(const timekeep_t tau)
{
	return ((((KPRECISIONI*tau)/(KPRECISIONI-1)) * 2 / KPRECISIONI) + 1);
}

/**
 * Validate a temperature value
 * @param temp the value to validate
 * @return validation result
 */
__attribute__((const, always_inline)) static inline int validate_temp(const temp_t temp)
{
	if ((temp <= RWCHCD_TEMPMIN) || (temp >= RWCHCD_TEMPMAX))
		return (-EINVALID);
	else
		return (ALL_OK);
}

/**
 * Reset an integral
 * @param intgrl data to reset
 */
__attribute__((always_inline)) static inline void reset_intg(struct s_temp_intgrl * const intgrl)
{
	assert(intgrl);
	if (intgrl->inuse)
		memset(intgrl, 0x00, sizeof(*intgrl));
}

#endif /* rwchcd_lib_h */
