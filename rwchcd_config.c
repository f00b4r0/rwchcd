//
//  rwchcd_config.c
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#include <stdlib.h>	// alloc/free
#include "rwchcd_lib.h"
#include "rwchcd_spi.h"
#include "rwchcd_config.h"

// Save and restore config to permanent storage
// XXX review handling of rWCHC_settings


struct s_config * config_new(void)
{
	struct s_config * const config = calloc(1, sizeof(struct s_config));

	return (config);
}

void config_del(struct s_config * config)
{
	free(config);
}

int config_init(struct s_config * const config)
{
	int ret, i = 0;

	if (!config)
		return (-EINVALID);

	// grab current config on the hardware
	do {
		ret = rwchcd_spi_settings_r(&(config->rWCHC_settings));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (-ESPI);

	return (ALL_OK);
}

int config_set_runmode(struct s_config * const config, enum e_runmode runmode)
{
	if (!config)
		return (-EINVALID);

	config->set_runmode = runmode;

	return (ALL_OK);
}

int config_set_building_tau(struct s_config * const config, time_t tau)
{
	if (!config)
		return (-EINVALID);

	config->building_tau = tau;

	return (ALL_OK);
}

int config_set_nsensors(struct s_config * const config, short nsensors)
{
	if (!config)
		return (-EINVALID);

	if (nsensors > RWCHC_NTSENSORS)
		return (-EINVALID);

	config->nsensors = nsensors;
	config->rWCHC_settings.addresses.nsensors = nsensors;

	return (ALL_OK);
}

int config_set_frostmin(struct s_config * const config, temp_t frostmin)
{
	if (!config)
		return (-EINVALID);

	if (validate_temp(frostmin) != ALL_OK)
		return (-EINVALID);

	config->limit_tfrostmin = frostmin;
	config->rWCHC_settings.limits.frost_tmin = (uint8_t)temp_to_celsius(frostmin);

	return (ALL_OK);
}

int config_set_outdoor_sensor(struct s_config * const config, tempid_t sensorid)
{
	if (!config)
		return (-EINVALID);

	if (sensorid > config->nsensors)
		return (-EINVALID);

	config->id_temp_outdoor = sensorid;
	config->rWCHC_settings.addresses.S_outdoor = sensorid;

	return (ALL_OK);
}

int config_save(const struct s_config * const config)
{
	int ret, i = 0;

	if (!config)
		return (-EINVALID);

	// XXX TODO save config

	// write hardware config
	do {
		ret = rwchcd_spi_settings_w(&(config->rWCHC_settings));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (-ESPI);

	i = 0;
	// save hardware config
	do {
		ret = rwchcd_spi_settings_s();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (-ESPI);
	return (ALL_OK);
}