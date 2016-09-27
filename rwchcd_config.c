//
//  rwchcd_config.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
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

int config_set_building_tau(struct s_config * const config, const time_t tau)
{
	if (!config)
		return (-EINVALID);

	config->building_tau = tau;

	return (ALL_OK);
}

int config_set_nsensors(struct s_config * const config, const int_fast16_t nsensors)
{
	if (!config)
		return (-EINVALID);

	if (nsensors > RWCHC_NTSENSORS)
		return (-EINVALID);

	config->nsensors = nsensors;
	config->rWCHC_settings.addresses.nsensors = nsensors;

	return (ALL_OK);
}

int config_set_tfrostmin(struct s_config * const config, const temp_t tfrostmin)
{
	if (!config)
		return (-EINVALID);

	if (validate_temp(tfrostmin) != ALL_OK)
		return (-EINVALID);

	config->limit_tfrostmin = tfrostmin;
	config->rWCHC_settings.limits.frost_tmin = (uint8_t)temp_to_celsius(tfrostmin);

	return (ALL_OK);
}

int config_set_tsummer(struct s_config * const config, const temp_t tsummer)
{
	if (!config)
		return (-EINVALID);

	if (validate_temp(tsummer) != ALL_OK)
		return (-EINVALID);

	config->limit_tsummer = tsummer;

	return (ALL_OK);
}

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

int config_save(const struct s_config * const config)
{
	int ret, i = 0;

	if (!config)
		return (-EINVALID);

	// XXX TODO save config

	// commit hardware config
	do {
		ret = rwchcd_spi_settings_w(&(config->rWCHC_settings));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (-ESPI);

	return (ALL_OK);	// XXX DISABLE SAVE TO FLASH FOR NOW
#warning Save to flash disabled
	i = 0;
	// save hardware config
	do {
		ret = rwchcd_spi_settings_s();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		return (-ESPI);
	return (ALL_OK);
}