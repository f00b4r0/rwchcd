//
//  config.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
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
#include "hardware.h"
#include "storage.h"
#include "config.h"

static const storage_version_t Config_sversion = 7;

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

		memcpy(config, &temp_config, sizeof(*config));
		
		// restore hardware bits
		hardware_config_setnsensors(config->nsensors);

		dbgmsg("config restored");
		
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
	if (!config)
		return (-EINVALID);

	// see if we can restore previous config
	if (config_restore(config) == ALL_OK)
		hardware_config_store();	// update hardware if inconsistent
	
	return (ALL_OK);
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
 * Set number of temperature samples for readouts.
 * @param config target config
 * @param nsamples number of samples
 * @return exec status
 */
int config_set_temp_nsamples(struct s_config * const config, const uint_fast8_t nsamples)
{
	if (!config)
		return (-EINVALID);

	if (!nsamples)
		return (-EINVALID);

	config->temp_nsamples = nsamples;

	return (ALL_OK);
}

/**
 * Set number of active sensors.
 * @param config target config
 * @param nsensors number of active sensors
 * @return exec status
 * @note nsensors will be treated as the id of the last active sensor.
 */
int config_set_nsensors(struct s_config * const config, const int_fast8_t nsensors)
{
	if (!config)
		return (-EINVALID);

	if (nsensors > RWCHCD_NTEMPS)
		return (-EINVALID);

	config->nsensors = nsensors;

	return (hardware_config_setnsensors(nsensors));
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
 * Set outdoor sensor id.
 * @param config target config
 * @param sensorid id of outdoor temperature sensor
 * @return exec status
 * @warning must be called *AFTER* config_set_nsensors()
 */
int config_set_outdoor_sensorid(struct s_config * const config, const tempid_t sensorid)
{
	if (!config)
		return (-EINVALID);

	if (!sensorid || (sensorid > config->nsensors))
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
	if (ALL_OK != ret) {
		dbgerr("storage_dump failed (%d)", ret);
		goto out;
	}
	
	// save to hardware
	ret = hardware_config_store();

out:
	return (ret);
}
