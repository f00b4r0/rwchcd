//
//  config.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Configuration functions implementation.
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

/**
 * Set outdoor temperature for summer switchover.
 * Defines the temperature at which all heating circuits will be unconditionally stopped.
 * @param config target config
 * @param tsummer target outdoor temp for switchover
 * @return exec status
 */
int config_set_tsummer(struct s_config * const config, const temp_t tsummer)
{
	if (!config)
		return (-EINVALID);

	if (validate_temp(tsummer) != ALL_OK)
		return (-EINVALID);

	config->limit_tsummer = tsummer;

	return (ALL_OK);
}

/**
 * Set outdoor temperature for frost protection.
 * Defines the temperature at which frost protection will be required active.
 * @param config target config
 * @param tfrost target outdoor temp for switchover
 * @return exec status
 */
int config_set_tfrost(struct s_config * const config, const temp_t tfrost)
{
	if (!config)
		return (-EINVALID);
	
	if (validate_temp(tfrost) != ALL_OK)
		return (-EINVALID);
	
	config->limit_tfrost = tfrost;
	
	return (ALL_OK);
}
