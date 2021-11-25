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

#include <assert.h>
#include <limits.h>	// CHAR_BIT

#include "rwchcd.h"
#include "timekeep.h"

// NB: we rely on the fact that gcc sign-extends
#define sign(x)		((x>>(sizeof(x)*CHAR_BIT-1))|1)		///< -1 if x<0, 1 if x>=0
#define zerosign(x)	((x>>(sizeof(x)*CHAR_BIT-1))|(!!x))	///< -1 if x<0, 1 if x>0, 0 if x==0

/** Temperature integral data */
struct s_temp_intgrl {
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
 * Convert delta from internal to Kelvin value.
 * @note Ensure this is only used in non-fast code path (dbgmsg, config handling...).
 * @param dtemp the internal delta value to be converted
 */
#define temp_to_deltaK(dtemp)		((float)((float)dtemp/KPRECISION))

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
	intgrl->integral = 0;
	intgrl->last_thrsh = 0;
	intgrl->last_temp = 0;
	intgrl->last_time = 0;
}

/**
 * Branchless saturated u32 add.
 * If the result is smaller than either of the initial values (checking either is enough), overflow occurred => return all bits set.
 * @param a first operand
 * @param b second operand
 * @return saturated a+b
 */
__attribute__((always_inline)) static inline uint32_t lib_satadd_u32(uint32_t a, uint32_t b)
{
	uint32_t c = a + b;
	c |= (uint32_t)-(c < a);
	return (c);
}

/**
 * Branchless saturated u32 sub.
 * The logic is the inverse from add: we only care for underflow (overflow is not possible).
 * Unsigned sub is a 2-complement add, the test is similar to add, the bitmask is simply inverted so that underflow zeroes out, otherwise we AND with all bits set (-1).
 * @param a first operand
 * @param b second operand
 * @return saturated a-b
 */
__attribute__((always_inline)) static inline uint32_t lib_satsub_u32(uint32_t a, uint32_t b)
{
	uint32_t c = a - b;
	c &= (uint32_t)-(c <= a);
	return (c);
}

/**
 * Branchless saturated u32 mul.
 * Use an intermediary 64bit var for result to check for overflow, AND with all bits set (-1) if overflow occured
 * @param a first operand
 * @param b second operand
 * @return saturated a*b
 */
__attribute__((always_inline)) static inline uint32_t lib_satmul_u32(uint32_t a, uint32_t b)
{
	uint32_t hi, lo;
	uint64_t temp = a;
	temp *= b;
	hi = (uint32_t)(temp >> 32);	// carry - i.e. overflow
	lo = (uint32_t)temp;
	return (lo | (uint32_t)-!!hi);
}

/**
 * Branchless satured s32 add.
 * Over/underflow can only happen if both operands have the same sign: then if result has opposite sign to the arguments, over/underflow occured.
 * Note: INT32_MIN = INT32_MAX + 1
 * @param a first operand
 * @param b second operand
 * @return saturated a+b
 */
__attribute__((always_inline)) static inline int32_t lib_satadd_s32(int32_t a, int32_t b)
{
	uint32_t ua = (uint32_t)a;
	uint32_t ub = (uint32_t)b;
	uint32_t uc = ua + ub;

	ua = (ua >> 31) + INT32_MAX;	// calculate saturated result using sign of first operand *without* changing it

	// compiler should resolve the following block branchless through a conditional move
	if ((int32_t)(~(ua ^ ub) & (ua ^ uc)) < 0)
		uc = ua;

	return ((int32_t)uc);
}

/**
 * Branchless satured s32 sub.
 * Over/underflow can only happen if both operands have opposite sign: then if result has opposite sign to the first arg, over/underflow occured.
 * Note: INT32_MIN = INT32_MAX + 1
 * @param a first operand
 * @param b second operand
 * @return saturated a-b
 */
__attribute__((always_inline)) static inline int32_t lib_satsub_s32(int32_t a, int32_t b)
{
	uint32_t ua = (uint32_t)a;
	uint32_t ub = (uint32_t)b;
	uint32_t uc = ua - ub;

	ua = (ua >> 31) + INT32_MAX;

	if ((int32_t)((ua ^ ub) & (ua ^ uc)) < 0)
		uc = ua;

	return ((int32_t)uc);
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

__attribute__((always_inline)) static inline int32_t lib_fpdiv_s32(const int32_t n, const int32_t d, const uint32_t scale)
{
	int64_t temp = n;
	temp *= scale;
	temp /= d;
	return ((int32_t)temp);
}

__attribute__((always_inline)) static inline uint32_t lib_fpdiv_u32(const uint32_t n, const uint32_t d, const uint32_t scale)
{
	uint64_t temp = n;
	temp *= scale;
	temp /= d;
	return ((uint32_t)temp);
}

#define LIB_DERIV_FPDEC		0x8000

#define temp_expw_deriv_mul(_a, _b)	lib_fpmul_s32(_a, _b, LIB_DERIV_FPDEC)

#define temp_expw_deriv_val(_deriv)	((_deriv)->derivative)

#endif /* rwchcd_lib_h */
