//
//  hw_backends/hw_backends.c
//  rwchcd
//
//  (C) 2018,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware backends interface implementation.
 * This file implements tools to register specific hardware backends with the
 * system; and to identify sensors and relays provided by these backends by their names.
 */

#include <string.h>	// memset/strdup
#include <stdlib.h>	// free
#include <assert.h>

#include "timekeep.h"
#include "hw_backends.h"

struct s_hw_backends HW_backends;	///<  hardware backends

/**
 * Find backend by name.
 * @param name name to look for
 * @return -ENOTFOUND if not found, backend id if found
 */
int hw_backends_bid_by_name(const char * const name)
{
	hwbid_t id;
	int ret = -ENOTFOUND;

	assert(name);

	for (id = 0; id < HW_backends.last; id++) {
		if (!strcmp(HW_backends.all[id].name, name)) {
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
	memset(&HW_backends, 0x00, sizeof(HW_backends));

	return (ALL_OK);
}

/**
 * Register a hardware backend.
 * If registration is possible, the backend will be registered with the system.
 * @param callbacks a populated, valid backend structure
 * @param priv backend-specific private data
 * @param name @b unique user-defined name for this backend
 * @return exec status
 */
int hw_backends_register(const struct s_hw_callbacks * const callbacks, void * const priv, const char * const name)
{
	hwbid_t id;
	char * str = NULL;
	struct s_hw_backend * bkend;

	// sanitize input: check that mandatory callbacks are provided
	if (!callbacks || !callbacks->setup || !callbacks->exit || !callbacks->online || !callbacks->offline || !name)
		return (-EINVALID);

	// name must be unique
	if (hw_backends_bid_by_name(name) >= 0)
		return (-EEXISTS);

	if (HW_backends.last >= HW_backends.n)
		return (-EOOM);		// out of space

	// clone name if any
	str = strdup(name);
	if (!str)
		return(-EOOM);

	id = HW_backends.last;

	// allocate new backend
	bkend = &HW_backends.all[id];

	// populate backend
	bkend->name = str;
	bkend->cb = callbacks;
	bkend->priv = priv;

	HW_backends.last++;

	// return backend id
	return (ALL_OK);
}

/**
 * Cleanup hardware backend system
 */
void hw_backends_exit(void)
{
	unsigned int id;

	// cleanup all backends
	for (id = 0; id < HW_backends.last; id++) {
		freeconst(HW_backends.all[id].name);
		HW_backends.all[id].name = NULL;
	}

	free(HW_backends.all);

	memset(&HW_backends, 0x00, sizeof(HW_backends));
}
