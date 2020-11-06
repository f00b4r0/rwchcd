//
//  lib.h
//  rwchcd
//
//  (C) 2016-2017,2019-2020 Thibaut VARENE
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
	tempdiff_t integral;		///< integral value in temp_t * seconds
	temp_t last_thrsh;		///< temperature threshold for integral calculation
	temp_t last_temp;		///< last recorded temperature value
	timekeep_t last_time;		///< last recorded temperature time
};

/** Temperature derivative data */
struct s_temp_deriv {
	tempdiff_t derivative;		///< derivative value in temp_t / timekeep_t
	temp_t last_temp;		///< last recorded temperature value
	timekeep_t last_time;		///< last recorded temperature time
};

float temp_to_celsius(const temp_t temp);
float temp_to_deltaK(const temp_t temp);

temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const timekeep_t tau, const timekeep_t dt);
tempdiff_t temp_lin_deriv(struct s_temp_deriv * const deriv, const temp_t new_temp, const timekeep_t new_time, const timekeep_t tau);
tempdiff_t temp_thrs_intg(struct s_temp_intgrl * const intgrl, const temp_t thrsh, const temp_t new_temp, const timekeep_t new_time,
		      const tempdiff_t tlow_jacket, const tempdiff_t thigh_jacket);

/**
 * Convert kelvin value to internal temp_t format (Kelvin * KPRECISION).
 * @note The preprocessor will do the right thing whether kelvin is a float or a native integer type.
 * @param kelvin temp value in Kelvin
 */
#define kelvin_to_temp(kelvin)		(temp_t)((kelvin) * KPRECISION)

/**
 * Convert celsius value to internal temp_t format (Kelvin * KPRECISION).
 * @note The preprocessor will do the right thing whether celsius is a float or a native integer type.
 * @param celsius temp value in Celsius
 */
#define celsius_to_temp(celsius)	kelvin_to_temp((celsius) + 273)

/**
 * Convert temperature from internal format to integer Kelvin (rounded)
 * @param temp temp value as temp_t
 */
#define temp_to_ikelvin(temp)		(int)((temp + KPRECISION/2)/KPRECISION)

/**
* Convert temperature from internal format to integer Kelvin (floored)
* @param temp temp value as temp_t
*/
#define temp_to_ikelvind(temp)		((temp)/KPRECISION)

/**
 * Convert a temperature delta (in Kelvin) to internal type.
 * @note The preprocessor will do the right thing whether delta is a float or a native integer type.
 * @param delta the delta value to be converted
 */
#define deltaK_to_temp(delta)		(temp_t)((delta) * KPRECISION)
#define deltaK_to_tempdiff(delta)	(tempdiff_t)((delta) * KPRECISION)

/**
 * Calculate the minimum time interval to use with temp_expw_mavg() for a given
 * tau.
 * This function 'ceils' the return value.
 * @param tau target tau
 * @return minimum usable time interval
 */
__attribute__((const, always_inline)) static inline timekeep_t expw_mavg_dtmin(const timekeep_t tau)
{
	return ((((KPRECISION*tau)/(KPRECISION-1)) * 2 / KPRECISION) + 1);
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

__attribute__((always_inline)) static inline int32_t lib_fpmul_s32(const int32_t a, const int32_t b, const uint32_t scale)
{
	int64_t temp = a;
	temp *= b;
	temp /= scale;
	return ((int32_t)temp);
}

__attribute__((always_inline)) static inline uint32_t lib_fpmul_u32(const uint32_t a, const uint32_t b, const uint32_t scale)
{
	uint64_t temp = a;
	temp *= b;
	temp /= scale;
	return ((uint32_t)temp);
}

#define LIB_DERIV_FPDEC		0x8000

#define temp_expw_deriv_mul(_a, _b)	lib_fpmul_s32(_a, _b, LIB_DERIV_FPDEC)

#define temp_expw_deriv_val(_deriv)	((_deriv)->derivative)

#endif /* rwchcd_lib_h */
