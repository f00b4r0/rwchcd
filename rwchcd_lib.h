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

short validate_temp(const temp_t temp);
temp_t get_temp(const tempid_t id);

static inline temp_t celsius_to_temp(const float celsius)
{
	return ((temp_t)((celsius + 273.15F)*100));
}

static inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/100.0F - 273.15F));
}

static inline temp_t delta_to_temp(const temp_t delta)
{
	return ((temp_t)(delta * 100));
}

#endif /* rwchcd_lib_h */
