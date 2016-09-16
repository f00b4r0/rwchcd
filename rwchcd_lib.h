//
//  rwchcd_lib.h
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#ifndef rwchcd_lib_h
#define rwchcd_lib_h

#include "rwchcd.h"

short validate_temp(const temp_t temp);
temp_t get_temp(const tempid_t id);

inline temp_t celsius_to_temp(const float celsius)
{
	return ((temp_t)((celsius + 273.15)*100));
}

inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/100.0 - 273.15));
}

inline temp_t delta_to_temp(const temp_t delta)
{
	return (delta * 100);
}

#endif /* rwchcd_lib_h */
