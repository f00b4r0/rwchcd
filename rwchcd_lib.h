//
//  rwchcd_lib.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_lib_h
#define rwchcd_lib_h

#include "rwchcd.h"

int validate_temp(const temp_t temp);
temp_t get_temp(const tempid_t id);

/**
 * Convert celsius value to internal temp_t format (Kelvin *100).
 * @param celsius temp value in Celsius
 * @return value converted to internal type
 */
static inline temp_t celsius_to_temp(const float celsius)
{
	return ((temp_t)((celsius + 273.15F)*100));
}

/**
 * Convert temperature from internal format to Celsius value.
 * @param temp temp value as temp_t
 * @return value converted to Celsius
 */
static inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/100.0F - 273.15F));
}

/**
 * Convert a temperature delta (in Kelvin) to internal type.
 * @param delta the delta value to be converted
 * @return the corresponding value expressed in internal temperature format.
 */
static inline temp_t deltaK_to_temp(const float delta)
{
	return ((temp_t)(delta * 100));
}

/** 
 * Convert delta from internal to Kelvin value.
 * @param temp the internal delta value to be converted
 * @return the value converted to Kelvin
 */
static inline float temp_to_deltaK(const temp_t temp)
{
	return ((float)((float)temp/100.F));
}

#endif /* rwchcd_lib_h */
