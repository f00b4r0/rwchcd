//
//  rwchcd_config.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Configuration functions implementation.
 */

#include <stdlib.h>	// alloc/free
#include <string.h>	// memcpy
#include <assert.h>

#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_storage.h"
#include "rwchcd_config.h"

static const storage_version_t Config_sversion = 3;

// XXX review handling of rWCHC_settings

/**
 * Allocate new config.
 * @return pointer to config
 */
struct s_config * config_new(void)
{
	struct s_config * const config = calloc(1, sizeof(struct s_config));

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

		dbgmsg("config restored");
		
		memcpy(config, &temp_config, sizeof(*config));
		
		config->restored = true;
	}
	else
		dbgerr("storage_fetch failed (%d)", ret);
	
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
	int ret;

	if (!config)
		return (-EINVALID);

	// see if we can restore previous config
	ret = config_restore(config);
	if (ALL_OK == ret) {
		ret = hardware_config_set(&(config->rWCHC_settings));	// update hardware if inconsistent
		return (ret);
	}
	
	// if we couldn't, copy current hardware settings to config
	ret = hardware_config_get(&(config->rWCHC_settings));

	return (ret);
}

/**
 * Set building constant.
 * @param config target config
 * @param tau building time constant
 * @return exec status
 */
int config_set_building_tau(struct s_config * const config, const time_t tau)
{
	if (!config)
		return (-EINVALID);

	config->building_tau = tau;

	return (ALL_OK);
}

/**
 * Set number of active sensors.
 * @param config target config
 * @param nsensors number of active sensors
 * @return exec status
 * @note nsensors will be treated as the id of the last active sensor.
 */
int config_set_nsensors(struct s_config * const config, const int_fast16_t nsensors)
{
	if (!config)
		return (-EINVALID);

	if (nsensors > RWCHCD_NTEMPS)
		return (-EINVALID);

	config->nsensors = nsensors;
	config->rWCHC_settings.addresses.nsensors = nsensors;

	return (ALL_OK);
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
 * Set outdoor sensor id.
 * @param config target config
 * @param sensorid id of outdoor temperature sensor
 * @return exec status
 */
int config_set_outdoor_sensorid(struct s_config * const config, const tempid_t sensorid)
{
	if (!config)
		return (-EINVALID);

	if (!sensorid || (sensorid > config->nsensors))
		return (-EINVALID);

	config->id_temp_outdoor = sensorid;
	config->rWCHC_settings.addresses.S_outdoor = sensorid-1;

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
	if (ALL_OK != ret) {
		dbgerr("storage_dump failed (%d)", ret);
		goto out;
	}
	
	// save to hardware
	ret = hardware_config_set(&(config->rWCHC_settings));

out:
	return (ret);
}