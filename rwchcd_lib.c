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
