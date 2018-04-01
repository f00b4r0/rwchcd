//
//  hardware.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global hardware interface implementation.
 */

#include <string.h>	// memset/strdup

#include "hardware.h"

#define HW_MAX_BKENDS	8

struct hardware_backend {
	struct {
		bool online;
	} run;
	const struct hardware_callbacks * cb;	///< hardware backend callbacks
	void * restrict priv;
	const char * restrict name;
};

struct hardware_backend * HW_backends[HW_MAX_BKENDS];

int hardware_init(void)
{
	memset(HW_backends, 0x00, sizeof(HW_backends));

	return (ALL_OK);
}

/**
 * Register a hardware backend.
 * This function will init the backend if registration is possible.
 * @param backend a populated, valid backend structure
 * @return negative error code or positive backend id
 */
int hardware_backend_register(const struct hardware_callbacks * const callbacks, void * const priv, const char * const name)
{
	int id, ret;
	char * str;

	// sanitize input: check that mandatory callbacks are provided
	if (!callbacks || !callbacks->init || !callbacks->exit || !callbacks->online || !callbacks->offline)
		return (-EINVALID);

	// find first available spot in array
	for (id = 0; id < ARRAY_SIZE(HW_backends); id++) {
		if (!HW_backends[id])
			break;
	}

	if (ARRAY_SIZE(HW_backends) == id)
		return (-EOOM);		// out of space

	// init backend
	ret = callbacks->init(priv);

	if (ALL_OK != ret)
		return (ret);		// init failed, give up

	// register backend

	if (name) {
		str = strdup(name);
		if (!str)
			return(-EOOM);

		HW_backends[id]->name = str;
	}

	HW_backends[id]->cb = callbacks;
	HW_backends[id]->priv = priv;

	// return backend id
	return (id);
}

/**
 * Bring all registered backends online.
 * @return exec status
 */
int hardware_online(void)
{
	int id, ret;
	bool fail = false;

	// bring all registered backends online
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++) {
		if (HW_backends[id]->cb->online) {
			ret = HW_backends[id]->cb->online(HW_backends[id]->priv);
			if (ALL_OK != ret) {
				fail = true;
				dbgerr("online() failed for %s", HW_backends[id]->name);
			}
			else
				HW_backends[id]->run.online = true;
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-EINVALID);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Collect inputs from hardware.
 * @return exec status
 */
int hardware_input(void)
{
	int id, ret;
	bool fail = false;

	// input registered backends
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++) {
		if (!HW_backends[id]->run.online)
			continue;

		if (HW_backends[id]->cb->input) {
			ret = HW_backends[id]->cb->input(HW_backends[id]->priv);
			if (ALL_OK != ret) {
				fail = true;
				dbgerr("input() failed for %s", HW_backends[id]->name);
			}
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-EINVALID);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Apply commands to hardware.
 * @return exec status
 */
int hardware_output(void)
{
	int id, ret;
	bool fail = false;

	// output registered backends
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++) {
		if (!HW_backends[id]->run.online)
			continue;

		if (HW_backends[id]->cb->output) {
			ret = HW_backends[id]->cb->output(HW_backends[id]->priv);
			if (ALL_OK != ret) {
				fail = true;
				dbgerr("output() failed for %s", HW_backends[id]->name);
			}
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-EINVALID);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Take all registered backends offline.
 * @return exec status
 */
int hardware_offline(void)
{
	int id, ret;
	bool fail = false;

	// take all registered backends offline
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++) {
		if (!HW_backends[id]->run.online)
			continue;

		if (HW_backends[id]->cb->offline) {
			ret = HW_backends[id]->cb->offline(HW_backends[id]->priv);
			if (ALL_OK != ret) {
				fail = true;
				dbgerr("offline() failed for %s", HW_backends[id]->name);
			}
			else
				HW_backends[id]->run.online = false;
		}
	}

	// fail if we have no registered backend
	if (!id)
		return (-EINVALID);
	// or if one of them returned error
	else if (fail)
		return (-EGENERIC);
	else
		return (ALL_OK);
}

/**
 * Exit hardware subsystem.
 */
void hardware_exit(void)
{
	int id;

	// exit all registered backends
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++)
		HW_backends[id]->cb->exit(HW_backends[id]->priv);
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

	// make sure sid is non null
	if (!tempid.sid)
		return (-EINVALID);

	// make sure bid is valid
	if (ARRAY_SIZE(HW_backends) < bid)
		return (-EINVALID);

	if (!HW_backends[bid])
		return (-EINVALID);

	// make sure backend is online
	if (!HW_backends[bid]->run.online)
		return (-EOFFLINE);

	// make sure backend supports sensor_temp_clone
	if (!HW_backends[bid]->cb->sensor_clone_temp)
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends[bid]->cb->sensor_clone_temp(HW_backends[bid]->priv, tempid.sid, ctemp));
}

/**
 * Clone hardware sensor last update time.
 * @param tempid id (for the specified backend) of the hardware sensor to query
 * @param clast pointer to target to store the time value
 * @return exec status
 */
int hardware_sensor_clone_time(const tempid_t tempid, time_t * const clast)
{
	const bid_t bid = tempid.bid;

	// make sure sid is non null
	if (!tempid.sid)
		return (-EINVALID);

	// make sure bid is valid
	if (ARRAY_SIZE(HW_backends) < bid)
		return (-EINVALID);

	if (!HW_backends[bid])
		return (-EINVALID);

	// make sure backend is online
	if (!HW_backends[bid]->run.online)
		return (-EOFFLINE);

	// make sure backend supports op
	if (!HW_backends[bid]->cb->sensor_clone_time)
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends[bid]->cb->sensor_clone_time(HW_backends[bid]->priv, tempid.sid, clast));
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

	// make sure rid is non null
	if (!relid.rid)
		return (-EINVALID);

	// make sure bid is valid
	if (ARRAY_SIZE(HW_backends) < bid)
		return (-EINVALID);

	if (!HW_backends[bid])
		return (-EINVALID);

	// make sure backend is online
	if (!HW_backends[bid]->run.online)
		return (-EOFFLINE);

	// make sure backend supports relay_get_state
	if (!HW_backends[bid]->cb->relay_get_state)
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends[bid]->cb->relay_get_state(HW_backends[bid]->priv, relid.rid));
}

/**
 * Set relay state (request)
 * @param relid id (for the specified backend) of the hardware relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hardware_output()
 */
int hardware_relay_set_state(const relid_t relid, bool turn_on, time_t change_delay)
{
	const bid_t bid = relid.bid;

	// make sure rid is non null
	if (!relid.rid)
		return (-EINVALID);

	// make sure bid is valid
	if (ARRAY_SIZE(HW_backends) < bid)
		return (-EINVALID);

	if (!HW_backends[bid])
		return (-EINVALID);

	// make sure backend is online
	if (!HW_backends[bid]->run.online)
		return (-EOFFLINE);

	// make sure backend supports relay_set_state
	if (!HW_backends[bid]->cb->relay_set_state)
		return (-ENOTIMPLEMENTED);

	// call backend callback - input sanitizing left to cb
	return (HW_backends[bid]->cb->relay_set_state(HW_backends[bid]->priv, relid.rid, turn_on, change_delay));
}
