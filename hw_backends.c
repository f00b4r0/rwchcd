//
//  hw_backends.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware backends interface implementation.
 * This file implements tools to register specific hardware backends with the
 * system; and to identify sensors and relays provided by these backends by their names.
 * @todo Write a test backend to inject test values and register outputs for testing coverage
 */

#include <string.h>	// memset/strdup
#include <stdlib.h>	// free
#include <assert.h>

#include "timekeep.h"
#include "hw_backends.h"

struct s_hw_backend * HW_backends[];	///< Array of available hardware backends

/**
 * Find backend by name.
 * @param name name to look for
 * @return -ENOTFOUND if not found, backend id if found
 */
static int hw_backends_bid_by_name(const char * const name)
{
	unsigned int id;
	int ret = -ENOTFOUND;

	assert(name);

	for (id = 0; (id < ARRAY_SIZE(HW_backends) && HW_backends[id]); id++) {
		if (!strcmp(HW_backends[id]->name, name)) {
			// assert id < INT_MAX
			ret = (int)id;
			break;
		}
	}

	return (ret);
}

/**
 * Init hardware backend management.
 * This function clears internal backend state.
 */
int hw_backends_init(void)
{
	memset(HW_backends, 0x00, sizeof(HW_backends));

	return (ALL_OK);
}

/**
 * Register a hardware backend.
 * If registration is possible, the backend will be registered with the system.
 * @param callbacks a populated, valid backend structure
 * @param priv backend-specific private data
 * @param name @b unique user-defined name for this backend
 * @return negative error code or positive backend id
 */
int hw_backends_register(const struct s_hw_callbacks * const callbacks, void * const priv, const char * const name)
{
	unsigned int id;
	char * str = NULL;
	struct s_hw_backend * bkend;

	// sanitize input: check that mandatory callbacks are provided
	if (!callbacks || !callbacks->init || !callbacks->exit || !callbacks->online || !callbacks->offline || !name)
		return (-EINVALID);

	// name must be unique
	if (hw_backends_bid_by_name(name) >= 0)
		return (-EEXISTS);

	// find first available spot in array
	for (id = 0; id < ARRAY_SIZE(HW_backends); id++) {
		if (!HW_backends[id])
			break;
	}

	if (ARRAY_SIZE(HW_backends) == id)
		return (-EOOM);		// out of space

	// clone name if any
	str = strdup(name);
	if (!str)
		return(-EOOM);

	// allocate new backend
	bkend = calloc(1, sizeof(*bkend));
	if (!bkend) {
		free(str);
		return (-EOOM);
	}

	// populate backend
	bkend->name = str;
	bkend->cb = callbacks;
	bkend->priv = priv;

	// register backend
	HW_backends[id] = bkend;

	// return backend id
	// assert id < INT_MAX
	return ((int)id);
}

/**
 * Find a registered backend sensor by name.
 * This function finds the sensor named sensor_name in backend named bkend_name
 * and populates tempid with these elements.
 * @param tempid target tempid_t to populate
 * @param bkend_name name of the backend to look into
 * @param sensor_name name of the sensor to look for in that backend
 * @return execution status
 */
int hw_backends_sensor_fbn(tempid_t * tempid, const char * const bkend_name, const char * const sensor_name)
{
	bid_t bid;
	sid_t sid;
	int ret;

	// input sanitization
	if (!tempid || !bkend_name || !sensor_name)
		return (-EINVALID);

	// find backend
	ret = hw_backends_bid_by_name(bkend_name);
	if (ret < 0)
		return (-ENOTFOUND);

	bid = (bid_t)ret;

	if (!HW_backends[bid]->cb->sensor_ibn)
		return (-ENOTIMPLEMENTED);

	// find sensor in that backend
	ret = HW_backends[bid]->cb->sensor_ibn(HW_backends[bid]->priv, sensor_name);
	if (ret < 0)
		return (-ENOTFOUND);

	sid = (sid_t)ret;

	// populate target
	tempid->bid = bid;
	tempid->sid = sid;

	return (ALL_OK);
}

/**
 * Find a registered backend relay by name.
 * This function finds the relay named relay_name in backend named bkend_name
 * and populates relid with these elements.
 * @param relid target relid_t to populate
 * @param bkend_name name of the backend to look into
 * @param relay_name name of the relay to look for in that backend
 * @return execution status
 */
int hw_backends_relay_fbn(relid_t * relid, const char * const bkend_name, const char * const relay_name)
{
	bid_t bid;
	rid_t rid;
	int ret;

	// input sanitization
	if (!relid || !bkend_name || !relay_name)
		return (-EINVALID);

	// find backend
	ret = hw_backends_bid_by_name(bkend_name);
	if (ret < 0)
		return (-ENOTFOUND);

	bid = (bid_t)ret;

	if (!HW_backends[bid]->cb->relay_ibn)
		return (-ENOTIMPLEMENTED);

	// find relay in that backend
	ret = HW_backends[bid]->cb->relay_ibn(HW_backends[bid]->priv, relay_name);
	if (ret < 0)
		return (-ENOTFOUND);

	rid = (rid_t)ret;
	
	// populate target
	relid->bid = bid;
	relid->rid = rid;

	return (ALL_OK);
}

/**
 * Cleanup hardware backend system
 */
void hw_backends_exit(void)
{
	unsigned int id;

	// exit all registered backends
	for (id = 0; HW_backends[id] && (id < ARRAY_SIZE(HW_backends)); id++) {
		if (HW_backends[id]->name) {
			free((void *)HW_backends[id]->name);
			HW_backends[id]->name = NULL;
		}
		free(HW_backends[id]);
		HW_backends[id] = NULL;
	}

}

/**
 * Return a backend name.
 * @param bid target backend id
 * @return target backend name or NULL if error.
 */
const char * hw_backends_name(const bid_t bid)
{
	if ((bid >= ARRAY_SIZE(HW_backends)) || !HW_backends[bid])
		return (NULL);

	return (HW_backends[bid]->name);
}
