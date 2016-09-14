//
//  rwchcd_lib.c
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#include "rwchcd.h"

short validate_temp(const temp_t temp)
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
 * @return temp if id valid, 0 otherwise
 */
temp_t get_temp(const tempid_t id)
{
	const struct s_runtime * const runtime = get_runtime();

	if (id > runtime->config->nsensors)
		return (0);

	return (runtime->temps[id]);	// XXX REVISIT lock
}