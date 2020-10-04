//
//  io/outputs.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global outputs interface implementation.
 *
 * This subsystem interfaces between the hardware backends and the actuators requests. No request should ever directly address the backends,
 * instead they should use this interface,.
 */

#include <stdlib.h>
#include <string.h>

#include "rwchcd.h"
#include "relay.h"
#include "outputs.h"

struct s_outputs Outputs;

// Workaround to disambiguate 0 orid
#define outputs_orid_to_id(x)	((typeof(x))(x-1))
#define outputs_id_to_orid(x)	((typeof(x))(x+1))

/**
 * Init outputs system.
 * This function clears internal  state.
 */
int outputs_init(void)
{
	memset(&Outputs, 0x00, sizeof(Outputs));

	return (ALL_OK);
}

/**
 * Find a relay output by name.
 * @param name the unique name to look for
 * @return the relay output id or error status
 */
int outputs_relay_fbn(const char * name)
{
	orid_t id;
	int ret = -ENOTFOUND;

	if (!name || !strlen(name))
		return (-EINVALID);

	for (id = 0; id < Outputs.relays.last; id++) {
		if (!strcmp(Outputs.relays.all[id].name, name)) {
			ret = (int)outputs_id_to_orid(id);
			break;
		}
	}

	return (ret);
}

/**
 * Return a relay output name.
 */
const char * outputs_relay_name(const orid_t rid)
{
	const orid_t id = outputs_orid_to_id(rid);

	if (unlikely(id >= Outputs.relays.last))
		return (NULL);

	return (Outputs.relays.all[id].name);
}

/**
 * Get a relay output value.
 * @param rid the relay output id to act on
 * @param turn_on the requested state for the relay
 * @return exec status
 */
int outputs_relay_state_set(const orid_t rid, const bool turn_on)
{
	const orid_t id = outputs_orid_to_id(rid);

	if (unlikely(id >= Outputs.relays.last))
		return (-EINVALID);

	return (relay_state_set(&Outputs.relays.all[id], turn_on));
}

/**
 * Get a relay output last update time.
 * @param rid the relay output id to read from
 * @return relay state or error
 */
int outputs_relay_state_get(const orid_t rid)
{
	const orid_t id = outputs_orid_to_id(rid);

	if (unlikely(id >= Outputs.relays.last))
		return (-EINVALID);

	return (relay_state_get(&Outputs.relays.all[id]));
}

/**
 * Cleanup outputs system
 */
void outputs_exit(void)
{
	orid_t id;

	// clean all registered relays
	for (id = 0; id < Outputs.relays.last; id++)
		relay_clear(&Outputs.relays.all[id]);

	free((void *)Outputs.relays.all);
	memset(&Outputs, 0x00, sizeof(Outputs));
}
