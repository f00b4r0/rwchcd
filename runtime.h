//
//  runtime.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Runtime API.
 */

#ifndef rwchcd_runtime_h
#define rwchcd_runtime_h

#include "rwchcd.h"

/** Runtime environment structure */
struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode runmode;		///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
	bool summer;			///< outdoor temperature is compatible with summer mode
	bool frost;			///< outdoor temperature requires frost protection
	bool plant_could_sleep;		///< true if all heat sources could sleep (plant could sleep)
	bool dhwc_absolute;		///< true if absolute DHWT charge in progress
	bool dhwc_sliding;		///< true if sliding DHWT charge in progress
	temp_t t_outdoor;		///< instantaneous outdoor temperature
	temp_t t_outdoor_60;		///< t_outdoor filtered over a 60s window
	temp_t plant_hrequest;		///< local heat request
	temp_t external_hrequest;	///< external heat request (for cascading). @todo XXX NOT IMPLEMENTED
	time_t start_time;		///< system start time
	time_t consumer_sdelay;		///< minimum time consumers should keep their current consumption before turning off
	time_t temps_time;		///< time of temperatures last update
	int_fast16_t consumer_shift;	///< a factor to inhibit (negative) or increase (positive) consummers' heat requests. @todo XXX REVIEW
	struct s_plant * restrict plant;	///< running plant
	struct s_models * restrict models;	///< running models
	struct s_config * restrict config;	///< running config
	pthread_rwlock_t runtime_rwlock;///< @note having this here prevents using "const" in instances where it would otherwise be possible
};

struct s_runtime * get_runtime(void);
temp_t get_temp(const tempid_t id) __attribute__((deprecated));
int clone_temp(const tempid_t id, temp_t * const temp) __attribute__((warn_unused_result));
int runtime_init(void);
int runtime_set_systemmode(const enum e_systemmode sysmode);
int runtime_set_runmode(const enum e_runmode runmode);
int runtime_set_dhwmode(const enum e_runmode dhwmode);
int runtime_online(void);
int runtime_run(void);
int runtime_offline(void);
void runtime_exit(void);

#endif /* rwchcd_runtime_h */
