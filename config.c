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
#include <string.h>	// memcpy
#include <assert.h>

#include "lib.h"
#include "hardware.h"	// hardware_sensor_clone_time() for outdoor sensor
#include "storage.h"
#include "config.h"

static const storage_version_t Config_sversion = 11;

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
 * Restore config from permanent storage.
 * @param config config that will be populated with restored elements if possible,
 * left untouched otherwise
 * @return exec status
 */
static int config_restore(struct s_config * const config)
{
	struct s_config temp_config;
	storage_version_t sversion;
	int ret;
	
	assert(config);
	
	config->restored = false;
	
	// try to restore last config
	ret = storage_fetch("config", &sversion, &temp_config, sizeof(temp_config));
	if (ALL_OK == ret) {
		if (Config_sversion != sversion)
			return (-EMISMATCH);

		memcpy(config, &temp_config, sizeof(*config));
		
		pr_log(_("System configuration restored"));
		
		config->restored = true;
	}
	
	return (ret);
}

/**
 * Init config.
 * Tries to restore config from permanent storage, otherwise will get current
 * hardware config
 * @param config config
 * @return exec status
 */
int config_init(struct s_config * const config)
{
	if (!config)
		return (-EINVALID);

	// see if we can restore previous config
	return (config_restore(config));
}

/**
 * Config exit.
 * Saves current config
 * @param config config
 */
void config_exit(struct s_config * const config)
{
	if (!config)
		return;
	
	// save current config
	config_save(config);
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

/**
 * Set outdoor sensor ID.
 * @param config target config
 * @param sensorid id of outdoor temperature sensor
 * @return exec status
 */
int config_set_outdoor_sensorid(struct s_config * const config, const tempid_t sensorid)
{
	if (!config)
		return (-EINVALID);

	config->id_temp_outdoor = sensorid;

	return (ALL_OK);
}

/**
 * Save config.
 * @param config target config
 * @return exec status
 */
int config_save(const struct s_config * const config)
{
	int ret;

	if (!config)
		return (-EINVALID);

	// save config
	ret = storage_dump("config", &Config_sversion, config, sizeof(*config));
	if (ALL_OK != ret)
		dbgerr("storage_dump failed (%d)", ret);

	return (ret);
}
