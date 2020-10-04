//
//  inputs.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global inputs interface implementation.
 *
 * This subsystem interfaces between the hardware backends and the data consumers. No consumer should ever directly address the backends,
 * instead they should use this interface,.
 */

#include <stdlib.h>
#include <string.h>

#include "rwchcd.h"
#include "temperature.h"
#include "inputs.h"

struct s_inputs Inputs;

// Workaround to disambiguate 0 itid
#define inputs_itid_to_id(x)	((typeof(x))(x-1))
#define inputs_id_to_itid(x)	((typeof(x))(x+1))

/**
 * Init inputs system.
 * This function clears internal  state.
 */
int inputs_init(void)
{
	memset(&Inputs, 0x00, sizeof(Inputs));

	return (ALL_OK);
}

/**
 * Find a temperature input by name.
 * @param name the unique name to look for
 * @return the temperature input id or error status
 */
int inputs_temperature_fbn(const char * name)
{
	itid_t id;
	int ret = -ENOTFOUND;

	if (!name || !strlen(name))
		return (-EINVALID);

	for (id = 0; id < Inputs.temps.last; id++) {
		if (!strcmp(Inputs.temps.all[id].name, name)) {
			ret = (int)inputs_id_to_itid(id);
			break;
		}
	}

	return (ret);
}

/**
 * Return a temperature input name.
 */
const char * inputs_temperature_name(const itid_t tid)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (NULL);

	return (Inputs.temps.all[id].name);
}

/**
 * Get a temperature input value.
 * @param tid the temperature input id to read from
 * @param tout an optional pointer to store the result
 * @return exec status
 */
int inputs_temperature_get(const itid_t tid, temp_t * const tout)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (-EINVALID);

	return (temperature_get(&Inputs.temps.all[id], tout));
}

/**
 * Get a temperature input last update time.
 * @param tid the temperature input id to read from
 * @param stamp an optional pointer to store the result
 * @return exec status
 * @note this function will @b not request an update of the underlying temperature
 */
int inputs_temperature_time(const itid_t tid, timekeep_t * const stamp)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (-EINVALID);

	return (temperature_time(&Inputs.temps.all[id], stamp));
}

/**
 * Cleanup inputs system
 */
void inputs_exit(void)
{
	itid_t id;

	// clean all registered temps
	for (id = 0; id < Inputs.temps.last; id++)
		temperature_clear(&Inputs.temps.all[id]);

	free((void *)Inputs.temps.all);
	memset(&Inputs, 0x00, sizeof(Inputs));
}
