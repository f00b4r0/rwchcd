//
//  hardware.c
//  rwchcd
//
//  (C) 2018,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global hardware interface implementation.
 * This implementation provides a unified API to operate hardware backends.
 * @todo implement exclusive relay use (should be a good idea esp for config validation).
 */

#include "hw_backends.h"
#include "hardware.h"

extern struct s_hw_backends HW_backends;

/**
 * Init all registered backends.
 * For all registered backends, this function execute the .init() backend callback
 * after sanity checks. If the call is successful, the backend is marked as init'd.
 * If the backend has already been init'd, this function does nothing.
 * @return exec status
 */
int hardware_init(void)
{
	int ret;
	unsigned int id;
	bool fail = false;

	// init all registered backends
	for (id = 0; id < HW_backends.last; id++) {
		if (HW_backends.all[id].run.initialized)
			continue;

		if (HW_backends.all[id].cb->init) {
			ret = HW_backends.all[id].cb->init(HW_backends.all[id].priv);
			if (ALL_OK != ret) {
				fail = true;
				pr_err(_("Failed to initialize backend \"%s\" (%d)"), HW_backends.all[id].name, ret);
			}
			else
				HW_backends.all[id].run.initialized = true;
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
				dbgerr("input() failed for \"%s\" (%d)", HW_backends.all[id].name, ret);
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
				dbgerr("output() failed for \"%s\" (%d)", HW_backends.all[id].name, ret);
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
 * Clone temp from a hardware sensor.
 * @param tempid id of the hardware sensor to query
 * @param ctemp pointer to target to store the temperature value
 * @return exec status
 */
int hardware_sensor_clone_temp(const tempid_t tempid, temp_t * const ctemp)
{
	const bid_t bid = tempid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports sensor_temp_clone
	if (unlikely(!HW_backends.all[bid].cb->sensor_clone_temp))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->sensor_clone_temp(HW_backends.all[bid].priv, tempid.sid, ctemp));
}

/**
 * Clone hardware sensor last update time.
 * @note This function must @b ALWAYS return successfully if the target sensor is properly configured.
 * @param tempid id of the hardware sensor to query
 * @param clast pointer to target to store the time value
 * @return exec status
 */
int hardware_sensor_clone_time(const tempid_t tempid, timekeep_t * const clast)
{
	const bid_t bid = tempid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports op
	if (unlikely(!HW_backends.all[bid].cb->sensor_clone_time))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->sensor_clone_time(HW_backends.all[bid].priv, tempid.sid, clast));
}

/**
 * Return hardware sensor name.
 * @param tempid id of the target hardware sensor
 * @return target hardware sensor name or NULL if error
 */
const char * hardware_sensor_name(const tempid_t tempid)
{
	const bid_t bid = tempid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->sensor_name(HW_backends.all[bid].priv, tempid.sid));
}

/**
 * Get relay state (request).
 * Returns current state
 * @param relid id of the hardware relay to query
 * @return true if relay is on, false if off, negative if error.
 */
int hardware_relay_get_state(const relid_t relid)
{
	const bid_t bid = relid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports relay_get_state
	if (unlikely(!HW_backends.all[bid].cb->relay_get_state))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->relay_get_state(HW_backends.all[bid].priv, relid.rid));
}

/**
 * Set relay state (request)
 * @param relid id of the hardware relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hardware_output()
 */
int hardware_relay_set_state(const relid_t relid, bool turn_on)
{
	const bid_t bid = relid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (-EINVALID);

	// make sure backend is online
	if (unlikely(!HW_backends.all[bid].run.online))
		return (-EOFFLINE);

	// make sure backend supports relay_set_state
	if (unlikely(!HW_backends.all[bid].cb->relay_set_state))
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->relay_set_state(HW_backends.all[bid].priv, relid.rid, turn_on));
}

/**
 * Return hardware relay name.
 * @param relid id of the target hardware relay
 * @return target hardware relay name or NULL if error
 */
const char * hardware_relay_name(const relid_t relid)
{
	const bid_t bid = relid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->relay_name(HW_backends.all[bid].priv, relid.rid));
}
