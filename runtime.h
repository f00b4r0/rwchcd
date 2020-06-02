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
#include "timekeep.h"

/** Runtime environment structure */
struct s_runtime {
	struct {
		bool configured;
		enum e_systemmode startup_sysmode;	///< sysmode applied at startup
		enum e_runmode startup_runmode;		///< if sysmode is SYS_MANUAL, this runtime runmode will be applied
		enum e_runmode startup_dhwmode;		///< if sysmode is SYS_MANUAL, this runtime dhwmode will be applied
	} set;
	struct {
		enum e_systemmode systemmode;	///< current operation mode
		enum e_runmode runmode;		///< CANNOT BE #RM_AUTO
		enum e_runmode dhwmode;		///< CANNOT BE #RM_AUTO or #RM_DHWONLY
	} run;
	struct s_plant * restrict plant;	///< running plant
	pthread_rwlock_t runtime_rwlock;///< @note having this here prevents using "const" in instances where it would otherwise be possible
};

struct s_runtime * runtime_get(void);
int runtime_init(void);
int runtime_set_systemmode(const enum e_systemmode sysmode);
int runtime_set_runmode(const enum e_runmode runmode);
int runtime_set_dhwmode(const enum e_runmode dhwmode);
int runtime_online(void);
int runtime_run(void);
int runtime_offline(void);
void runtime_exit(void);

#endif /* rwchcd_runtime_h */
