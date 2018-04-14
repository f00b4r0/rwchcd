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
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	temp_t limit_tfrost;		///< outdoor temp for plant frost protection
	time_t sleeping_delay;		///< if no circuit request for this much time, then plant could sleep
	struct s_circuit_params def_circuit;	///< circuit defaults: if individual circuits don't set these values, these defaults will be used
	struct s_dhwt_params def_dhwt;		///< DHWT defaults: if individual dhwts don't set these values, these defaults will be used
};

struct s_config * config_new(void);
void config_del(struct s_config * config);
int config_init(struct s_config * const config);

int config_set_tsummer(struct s_config * const config, const temp_t tsummer);
int config_set_tfrost(struct s_config * const config, const temp_t tfrost);
int config_save(const struct s_config * const config);
void config_exit(struct s_config * const config);

#endif /* rwchcd_config_h */
