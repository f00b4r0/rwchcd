//
//  plant/pump.c
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
#include "io/outputs.h"

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
	int ret;

	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	ret = outputs_relay_state_get(pump->set.rid_pump);
	if (ret < 0) {
		pr_err(_("\"%s\": failed to get relay state (%d)"), pump->name, ret);
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
	return (outputs_relay_state_get(pump->set.rid_pump));
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
	(void)!outputs_relay_state_set(pump->set.rid_pump, false);

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
	const timekeep_t now = timekeep_now();
	timekeep_t elapsed;
	bool state;
	int ret;

	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!pump->run.online))	// implies set.configured == true
		return (-EOFFLINE);

	pump->run.active = true;	// XXX never set false because we don't really need to for now

	state = !!outputs_relay_state_get(pump->set.rid_pump);	// assumed cannot fail
	if (state == pump->run.req_on)
		return (ALL_OK);

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!pump->run.req_on && !pump->run.force_state) {
		elapsed = now - pump->run.last_switch;
		if (elapsed < pump->set.cooldown_time)
			return (ALL_OK);
	}

	// this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	ret = outputs_relay_state_set(pump->set.rid_pump, pump->run.req_on);
	if (unlikely(ret < 0))
		return (ret);

	pump->run.last_switch = now;

	return (ALL_OK);
}

