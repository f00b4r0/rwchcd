//
//  rwchcd_lib.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include "rwchcd.h"
#include "rwchcd_runtime.h"

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