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
	bool plant_could_sleep;		///< true if all heat sources could sleep (plant could sleep)
	bool summer;			///< outdoor temperature is compatible with summer mode
	bool frost;			///< outdoor temperature requires frost protection
	temp_t t_outdoor;		///< instantaneous outdoor temperature
	temp_t t_outdoor_60;		///< t_outdoor filtered over a 60s window
	temp_t external_hrequest;	///< external heat request (for cascading). @todo XXX NOT IMPLEMENTED
	time_t start_time;		///< system start time
	struct s_plant * restrict plant;	///< running plant
	struct s_models * restrict models;	///< running models
	struct s_config * restrict config;	///< running config
	temp_t temps[RWCHCD_NTEMPS];		///< array of all the system temperatures
	pthread_rwlock_t runtime_rwlock;///< @note having this here prevents using "const" in instances where it would otherwise be possible
};

struct s_runtime * get_runtime(void);
int runtime_init(void);
int runtime_set_systemmode(const enum e_systemmode sysmode);
int runtime_set_runmode(const enum e_runmode runmode);
int runtime_set_dhwmode(const enum e_runmode dhwmode);
int runtime_online(void);
int runtime_run(void);
int runtime_offline(void);
void runtime_exit(void);

#endif /* rwchcd_runtime_h */
