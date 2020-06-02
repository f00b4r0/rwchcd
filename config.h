//
//  config.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * System-wide configuration API.
 */

#ifndef rwchcd_config_h
#define rwchcd_config_h

#include "rwchcd.h"
#include "timekeep.h"

/** Config structure */
struct s_config {
	bool configured;		///< true if properly configured
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	temp_t limit_tfrost;		///< outdoor temp for plant frost protection
	enum e_systemmode startup_sysmode;	///< sysmode applied at startup
	enum e_runmode startup_runmode;		///< if sysmode is SYS_MANUAL, this runtime runmode will be applied
	enum e_runmode startup_dhwmode;		///< if sysmode is SYS_MANUAL, this runtime dhwmode will be applied
};

struct s_config * config_new(void);
void config_del(struct s_config * config);

int config_set_tsummer(struct s_config * const config, const temp_t tsummer);
int config_set_tfrost(struct s_config * const config, const temp_t tfrost);

#endif /* rwchcd_config_h */
