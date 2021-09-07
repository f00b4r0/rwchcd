//
//  plant/pump.c
//  rwchcd
//
//  (C) 2017,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump operation implementation.
 *
 * The pump implementation supports:
 * - Cooldown timeout (to prevent short runs)
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * each pump is managed exclusively by a parent entity and thus no concurrent operation is
 * ever expected to happen to a given pump, with the exception of _get_state() which is thread-safe.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "pump.h"
#include "io/outputs.h"
#include "alarms.h"

/**
 * Cleanup a pump.
 * Frees all pump-local resources
 * @param pump the pump to delete
 */
void pump_cleanup(struct s_pump * restrict pump)
{
	if (!pump)
		return;

	free((void *)pump->name);
	pump->name = NULL;
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

	ret = outputs_relay_grab(pump->set.rid_pump);
	if (ALL_OK != ret) {
		pr_err(_("\"%s\": Pump relay is unavailable (%d)"), pump->name, ret);
		return (-EMISCONFIGURED);
	}

	aser(&pump->run.online, true);

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

	if (unlikely(!aler(&pump->run.online)))
		return (-EOFFLINE);

	aser(&pump->run.req_on, req_on);
	pump->run.force_state = force_state;

	return (ALL_OK);
}

/**
 * Get pump state.
 * @param pump target pump
 * @return pump state
 * @note thread-safe by virtue of only calling outputs_relay_state_get()
 */
int pump_get_state(const struct s_pump * restrict const pump)
{
	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!aler(&pump->run.online)))
		return (-EOFFLINE);

	// NOTE we could return remaining cooldown time if necessary
	return (outputs_relay_state_get(pump->set.rid_pump));
}

/**
 * Set pump DHWT use.
 * @param pump target pump
 * @param used set pump as used by DHWT if true
 * @return error code if any
 */
int pump_set_dhwt_use(struct s_pump * const pump, bool used)
{
	assert(pump);
	assert(aler(&pump->run.online));

	pump->run.dhwt_use = used;

	return (ALL_OK);
}

/**
 * Get pump DHWT use.
 * @param pump target pump
 * @return true if pump is used by DHWT
 */
int pump_get_dhwt_use(const struct s_pump * const pump)
{
	assert(pump);
	assert(aler(&pump->run.online));

	return (pump->run.dhwt_use);
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

	outputs_relay_thaw(pump->set.rid_pump);

	memset(&pump->run, 0x00, sizeof(pump->run));
	//pump->run.online = false;	// handled by memset

	return(ALL_OK);
}

#define pump_failsafe(_pump)	(void)pump_shutdown(_pump)

/**
 * Run pump.
 * @param pump target pump
 * @return exec status
 * @note this function ensures that in the event of an error, the pump is put in a
 * failsafe state as defined in pump_failsafe().
 */
int pump_run(struct s_pump * restrict const pump)
{
	const timekeep_t now = timekeep_now();
	timekeep_t elapsed;
	bool state, req;
	int ret;

	if (unlikely(!pump))
		return (-EINVALID);

	if (unlikely(!aler(&pump->run.online)))	// implies set.configured == true
		return (-EOFFLINE);

	dbgmsg(1, 1, "\"%s\": req: %d, force: %d", pump->name, aler(&pump->run.req_on), pump->run.force_state);

	state = !!outputs_relay_state_get(pump->set.rid_pump);	// assumed cannot fail
	req = aler(&pump->run.req_on);
	if (state == req)
		return (ALL_OK);

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!req && !pump->run.force_state) {
		elapsed = now - pump->run.last_switch;
		if (elapsed < pump->set.cooldown_time)
			return (ALL_OK);
	}

	ret = outputs_relay_state_set(pump->set.rid_pump, req);
	if (unlikely(ret < 0))
		goto fail;

	pump->run.last_switch = now;

	return (ALL_OK);

fail:
	alarms_raise(ret, _("Pump \"%s\": failed to operate!"), pump->name);
	pump_failsafe(pump);
	return (ret);
}

/**
 * Test if pump is online.
 * @param pump target pump
 * @return true if online, false otherwise
 */
bool pump_is_online(const struct s_pump * const pump)
{
	assert(pump);
	return (aler(&pump->run.online));
}
