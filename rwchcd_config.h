//
//  rwchcd_config.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_config_h
#define rwchcd_config_h

#include "rwchcd.h"

struct s_config * config_new(void);
void config_del(struct s_config * config);
int config_init(struct s_config * const config);
int config_set_building_tau(struct s_config * const config, time_t tau);
int config_set_nsensors(struct s_config * const config, short nsensors);
int config_set_tfrostmin(struct s_config * const config, temp_t tfrostmin);
int config_set_tsummer(struct s_config * const config, temp_t tsummer);
int config_set_outdoor_sensorid(struct s_config * const config, tempid_t sensorid);
int config_save(const struct s_config * const config);

#endif /* rwchcd_config_h */
