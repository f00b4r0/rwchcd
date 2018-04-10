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
 */

#include <string.h>	// memset/strdup
#include <stdlib.h>	// free
#include <time.h>	// time_t

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

	for (id = 0; (id < ARRAY_SIZE(HW_backends) && HW_backends[id]); id++) {
		if (!strcmp(HW_backends[id]->name, name)) {
			ret = id;
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
	return (id);
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
			free(HW_backends[id]->name);
			HW_backends[id]->name = NULL;
		}
		free(HW_backends[id]);
		HW_backends[id] = NULL;
	}

}


