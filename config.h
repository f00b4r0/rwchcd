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
	enum e_systemmode startup_sysmode;	///< sysmode applied at startup
	enum e_runmode startup_runmode;		///< if sysmode is SYS_MANUAL, this runtime runmode will be applied
	enum e_runmode startup_dhwmode;		///< if sysmode is SYS_MANUAL, this runtime dhwmode will be applied
};

struct s_config * config_new(void);
void config_del(struct s_config * config);

#endif /* rwchcd_config_h */
