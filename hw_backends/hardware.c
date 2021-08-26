//
//  hw_backends/hardware.c
//  rwchcd
//
//  (C) 2018,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global hardware interface implementation.
 * This implementation provides a unified API to operate hardware backends.
 *
 * @todo implement exclusive relay use (should be a good idea esp for config validation).
 * @note The plant runtime code "expects" that the outputs will be coalesced by the hardware backend,
 * so that a given output may be flipped multiple times within a single execution of a particular _run() function
 * and the hardware controller will still output the last state when hardware_output() is run.
 * This is particularly useful in the error path, where after setting an output to some state the _run() code may eventually fall back to a failsafe state before exit.
 */

#include "alarms.h"
#include "hw_backends/hw_backends.h"
#include "hardware.h"

extern struct s_hw_backends HW_backends;

/**
 * Setup all registered backends.
 * For all registered backends, this function execute the .setup() backend callback
 * after sanity checks. If the call is successful, the backend is marked as init'd.
 * If the backend has already been init'd, this function does nothing.
 * @return exec status
 */
int hardware_setup(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// setup all registered backends
	for (id = 0; id < HW_backends.last; id++) {
		if (HW_backends.all[id].run.initialized)
			continue;

		if (HW_backends.all[id].cb->setup) {
			ret = HW_backends.all[id].cb->setup(HW_backends.all[id].priv, HW_backends.all[id].name);
			if (ALL_OK != ret) {
				fail = true;
				pr_err(_("Failed to setup backend \"%s\" (%d)"), HW_backends.all[id].name, ret);
			}
			else
				HW_backends.all[id].run.initialized = true;
		}
	}

	// fail if one of them returned error
	if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Bring all registered backends online.
 * For all registered backends, this function execute the .online() backend callback
 * after sanity checks. If the call is successful, the backend is marked as online.
 * If the backend has already been online'd, this function does nothing.
 * @note if the backend provides sensors, after .online() is executed subsequent
 * calls to hardware_sensor_clone_time() must succeed (sensor is configured) @b EVEN if
 * hardware_input() hasn't yet been called. This is necessary for other subsystems
 * online() checks.
 * @return exec status
 */
int hardware_online(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// bring all registered backends online
	for (id = 0; id < HW_backends.last; id++) {
		if (HW_backends.all[id].run.online)
			continue;

		if (HW_backends.all[id].cb->online) {
			ret = HW_backends.all[id].cb->online(HW_backends.all[id].priv);
			if (ALL_OK != ret) {
				fail = true;
				pr_err(_("Failed to bring backend \"%s\" online (%d)"), HW_backends.all[id].name, ret);
			}
			else
				HW_backends.all[id].run.online = true;
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-ENOTCONFIGURED);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Collect inputs from hardware.
 * For all registered backends, this function execute the .input() backend callback
 * after sanity checks.
 * If the backend isn't online, this function does nothing.
 * @return exec status
 */
int hardware_input(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// input registered backends
	for (id = 0; id < HW_backends.last; id++) {
		if (unlikely(!HW_backends.all[id].run.online))
			continue;

		if (likely(HW_backends.all[id].cb->input)) {
			ret = HW_backends.all[id].cb->input(HW_backends.all[id].priv);
			if (unlikely(ALL_OK != ret)) {
				fail = true;
				alarms_raise(ret, _("Backend \"%s\": input() failed"), HW_backends.all[id].name);
			}
		}
	}

	// fail if we have no registered backend
	if (unlikely(!id))
		return (-ENOTCONFIGURED);
	// or if one of them returned error
	else if (unlikely(fail))
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Output data to hardware.
 * For all registered backends, this function execute the .output() backend callback
 * after sanity checks.
 * If the backend isn't online, this function does nothing.
 * @return exec status
 */
int hardware_output(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// output registered backends
	for (id = 0; id < HW_backends.last; id++) {
		if (unlikely(!HW_backends.all[id].run.online))
			continue;

		if (likely(HW_backends.all[id].cb->output)) {
			ret = HW_backends.all[id].cb->output(HW_backends.all[id].priv);
			if (unlikely(ALL_OK != ret)) {
				fail = true;
				alarms_raise(ret, _("Backend \"%s\": output() failed"), HW_backends.all[id].name);
			}
		}
	}

	// fail if we have no registered backend
	if (unlikely(!id))
		return (-ENOTCONFIGURED);
	// or if one of them returned error
	else if (unlikely(fail))
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Take all registered backends offline.
 * For all registered backends, this function execute the .offline() backend callback
 * after sanity checks.
 * If the backend isn't online, this function does nothing.
 * @return exec status
 */
int hardware_offline(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// take all registered backends offline
	for (id = 0; id < HW_backends.last; id++) {
		if (!HW_backends.all[id].run.online)
			continue;

		if (HW_backends.all[id].cb->offline) {
			ret = HW_backends.all[id].cb->offline(HW_backends.all[id].priv);
			if (ALL_OK != ret) {
				fail = true;
				pr_err(_("Failed to bring backend \"%s\" offline (%d)"), HW_backends.all[id].name, ret);
			}
			else
				HW_backends.all[id].run.online = false;
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-ENOTCONFIGURED);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Exit hardware subsystem.
 * For all registered backends, this function execute the .exit() backend callback
 * after sanity checks, and frees resources.
 * @note Backend's exit() routine MUST release memory in @b priv if necessary.
 */
void hardware_exit(void)
{
	unsigned int id;

	// exit all registered backends
	for (id = 0; id < HW_backends.last; id++)
		HW_backends.all[id].cb->exit(HW_backends.all[id].priv);
}

/**
 * Get value from a hardware input.
 * @param binid id of the hardware input to query
 * @param type the type of requested input
 * @param value pointer to target to store the input value
 * @return exec status
 */
int hardware_input_value_get(const binid_t binid, const enum e_hw_input_type type, u_hw_in_value_t * const value)
{
	const bid_t bid = binid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports op
	if (unlikely(!HW_backends.all[bid].cb->input_value_get))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->input_value_get(HW_backends.all[bid].priv, type, binid.inid, value));
}

/**
 * Get last update time from hardware input.
 * @note This function must @b ALWAYS return successfully if the target sensor is properly configured.
 * @param binid id of the hardware input to query
 * @param type the type of requested input
 * @param clast pointer to target to store the time value
 * @return exec status
 */
int hardware_input_time_get(const binid_t binid, const enum e_hw_input_type type, timekeep_t * const clast)
{
	const bid_t bid = binid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports op
	if (unlikely(!HW_backends.all[bid].cb->input_time_get))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->input_time_get(HW_backends.all[bid].priv, type, binid.inid, clast));
}

/**
 * Get hardware output state.
 * @param boutid id of the hardware input to query
 * @param type the type of requested input
 * @param state pointer to target to store the input value
 * @return exec status
 * @deprecated this function probably doesn't make much sense in the current code, it isn't used anywhere and might be removed in the future
 */
int hardware_output_state_get(const boutid_t boutid, const enum e_hw_output_type type, u_hw_out_state_t * const state)
{
	const bid_t bid = boutid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports op
	if (unlikely(!HW_backends.all[bid].cb->output_state_get))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->output_state_get(HW_backends.all[bid].priv, type, boutid.outid, state));
}

/**
 * Set hardware output state.
 * @param boutid id of the hardware input to query
 * @param type the type of requested input
 * @param state pointer to requested output value
 * @return exec status
 * @note actual (hardware) output state will only be updated by a call to hardware_output()
 */
int hardware_output_state_set(const boutid_t boutid, const enum e_hw_output_type type, const u_hw_out_state_t * const state)
{
	const bid_t bid = boutid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports op
	if (unlikely(!HW_backends.all[bid].cb->output_state_set))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->output_state_set(HW_backends.all[bid].priv, type, boutid.outid, state));
}
