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
 *
 * The pump implementation supports:
 * - Cooldown timeout (to prevent short runs)
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset

#include "pump.h"
#include "hardware.h"

/**
 * Create a pump.
 * @return the newly created pump or NULL
 */
struct s_pump * pump_new(void)
{
	struct s_pump * const pump = calloc(1, sizeof(*pump));
	return (pump);
}

/**
 * Delete a pump.
 * Frees all pump-local resources
 * @param pump the pump to delete
 */
void pump_del(struct s_pump * restrict pump)
{
	if (!pump)
		return;

	free((void *)pump->name);
	pump->name = NULL;
	free(pump);
}

/**
 * Put pump online.
 * Perform all necessary actions to prepare the pump for service
 * and mark it as online.
 * @param pump target pump
 * @return exec status
 */
int pump_online(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->set.rid_pump.rid) {
		pr_err(_("\"%s\": invalid relay id"), pump->name);
		return (-EMISCONFIGURED);
	}

	pump->run.online = true;

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
	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!pump->run.online))
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
	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!pump->run.online))
		return (-EOFFLINE);

	// NOTE we could return remaining cooldown time if necessary
	return (hardware_relay_get_state(pump->set.rid_pump));
}

/**
 * Shutdown an online pump.
 * Perform all necessary actions to completely shut down the pump.
 * @param pump target pump
 * @return exec status
 */
int pump_shutdown(struct s_pump * restrict const pump)
{
	if (unlikely(!pump))
		return (-EINVALID);

	if (!pump->run.active)
		return (ALL_OK);

	return(pump_set_state(pump, OFF, FORCE));
}

/**
 * Put pump offline.
 * Perform all necessary actions to completely shut down the pump
 * and mark it as offline.
 * @param pump target pump
 * @return exec status
 */
int pump_offline(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	// unconditionally turn pump off
	(void)!hardware_relay_set_state(pump->set.rid_pump, false, 0);

	memset(&pump->run, 0x00, sizeof(pump->run));
	//pump->run.online = false;	// handled by memset

	return(ALL_OK);
}

/**
 * Run pump.
 * @param pump target pump
 * @return exec status
 */
int pump_run(struct s_pump * restrict const pump)
{
	timekeep_t cooldown = 0;	// by default, no wait
	int ret;

	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!pump->run.online))	// implies set.configured == true
		return (-EOFFLINE);

	pump->run.active = true;	// XXX never set false because we don't really need to for now

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!pump->run.req_on && !pump->run.force_state)
		cooldown = pump->run.actual_cooldown_time ? pump->run.actual_cooldown_time : pump->set.cooldown_time;

	// this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	ret = hardware_relay_set_state(pump->set.rid_pump, pump->run.req_on, cooldown);
	if (unlikely(ret < 0))
		return (ret);

	pump->run.actual_cooldown_time = (timekeep_t)ret;

	return (ALL_OK);
}

