//
//  config.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Configuration functions API.
 */

#ifndef rwchcd_config_h
#define rwchcd_config_h

#include "rwchcd.h"

/** Config structure */
struct s_config {
	bool restored;			///< true if config has been restored from storage
	bool configured;		///< true if properly configured
	bool summer_maintenance;	///< true if pumps/valves should be run periodically in summer.
	uint_fast8_t temp_nsamples;	///< number of samples for temperature readout LP filtering
	uint_fast8_t nsensors;		///< number of active sensors (== id of last sensor)
	tempid_t id_temp_outdoor;	///< outdoor temp
	temp_t offset_toutdoor;		///< offset for outdoor temp sensor
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	temp_t limit_tfrost;		///< outdoor temp for plant frost protection
	time_t sleeping_delay;		///< if no circuit request for this much time, then plant could sleep
	struct s_circuit_params def_circuit;	///< circuit defaults: if individual circuits don't set these values, these defaults will be used
	struct s_dhwt_params def_dhwt;		///< DHWT defaults: if individual dhwts don't set these values, these defaults will be used
};

struct s_config * config_new(void);
void config_del(struct s_config * config);
int config_init(struct s_config * const config);
int config_set_temp_nsamples(struct s_config * const config, const uint_fast8_t nsamples);
int config_set_nsensors(struct s_config * const config, const int_fast8_t nsensors);
int config_set_tsummer(struct s_config * const config, const temp_t tsummer);
int config_set_tfrost(struct s_config * const config, const temp_t tfrost);
int config_set_outdoor_sensor(struct s_config * const config, const tempid_t sensorid, const temp_t offset);
int config_save_hw(const struct s_config * const config, const bool save_hw);
void config_exit(struct s_config * const config);

#define config_save(config)	config_save_hw(config, true)

#endif /* rwchcd_config_h */
