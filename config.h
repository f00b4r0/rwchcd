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
	bool summer_maintenance;	///< true if pumps/valves should be run periodically in summer. See #summer_run_interval and #summer_run_duration
	bool logging;			///< true if data logging should be enabled
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	temp_t limit_tfrost;		///< outdoor temp for plant frost protection
	timekeep_t sleeping_delay;		///< if no circuit request for this much time, then plant could sleep (will trigger electric switchover when available)
	timekeep_t summer_run_interval;		///< interval between summer maintenance runs (suggested: 1 week). @note if #summer_maintenance is true then this MUST be set
	timekeep_t summer_run_duration;		///< duration of summer maintenance operation (suggested: 10mn). @note if #summer_maintenance is true then this MUST be set
	enum e_systemmode startup_sysmode;	///< sysmode applied at startup
	enum e_runmode startup_runmode;		///< if sysmode is SYS_MANUAL, this runtime runmode will be applied
	enum e_runmode startup_dhwmode;		///< if sysmode is SYS_MANUAL, this runtime dhwmode will be applied
	struct s_hcircuit_params def_hcircuit;	///< heating circuit defaults: if individual hcircuits don't set these values, these defaults will be used
	struct s_dhwt_params def_dhwt;		///< DHWT defaults: if individual dhwts don't set these values, these defaults will be used
};

struct s_config * config_new(void);
void config_del(struct s_config * config);

int config_set_tsummer(struct s_config * const config, const temp_t tsummer);
int config_set_tfrost(struct s_config * const config, const temp_t tfrost);

#endif /* rwchcd_config_h */
