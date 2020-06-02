//
//  config.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * System-wide configuration implementation.
 */

#include <stdlib.h>	// alloc/free

#include "lib.h"
#include "config.h"

/**
 * Allocate new config.
 * @return pointer to config
 */
struct s_config * config_new(void)
{
	struct s_config * const config = calloc(1, sizeof(*config));

	return (config);
}

/**
 * Delete config
 * @param config pointer to config
 */
void config_del(struct s_config * config)
{
	free(config);
}
