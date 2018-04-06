//
//  pump.c
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>

#include "pump.h"
#include "hardware.h"

/**
 * Delete a pump
 * Frees all pump-local resources
 * @param pump the pump to delete
 */
void pump_del(struct s_pump * restrict pump)
{
	if (!pump)
		return;

	free(pump->name);
	pump->name = NULL;
	free(pump);
}

/**
 * Put pump online.
 * Perform all necessary actions to prepare the pump for service.
 * @param pump target pump
 * @return exec status
 */
int pump_online(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	return (ALL_OK);
}

/**
 * Set pump state.
 * @param pump target pump
 * @param req_on request pump on if true
 * @param force_state skips cooldown if true
 * @return error code if any
 */
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state)
{
	assert(pump);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->run.online)
		return (-EOFFLINE);

	pump->run.req_on = req_on;
	pump->run.force_state = force_state;

	return (ALL_OK);
}

/**
 * Get pump state.
 * @param pump target pump
 * @return pump state
 */
int pump_get_state(const struct s_pump * restrict const pump)
{
	assert(pump);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->run.online)
		return (-EOFFLINE);

	// NOTE we could return remaining cooldown time if necessary
	return (hardware_relay_get_state(pump->set.rid_relay));
}

/**
 * Put pump offline.
 * Perform all necessary actions to completely shut down the pump.
 * @param pump target pump
 * @return exec status
 */
int pump_offline(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	return(pump_set_state(pump, OFF, FORCE));
}

/**
 * Run pump.
 * @param pump target pump
 * @return exec status
 */
int pump_run(struct s_pump * restrict const pump)
{
	time_t cooldown = 0;	// by default, no wait
	int ret;

	assert(pump);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->run.online)
		return (-EOFFLINE);

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!pump->run.req_on && !pump->run.force_state)
		cooldown = pump->run.actual_cooldown_time ? pump->run.actual_cooldown_time : pump->set.cooldown_time;

	// this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	ret = hardware_relay_set_state(pump->set.rid_relay, pump->run.req_on, cooldown);
	if (ret < 0)
		return (ret);

	pump->run.actual_cooldown_time = ret;

	return (ALL_OK);
}
