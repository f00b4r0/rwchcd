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

#include <stdatomic.h>

/** Runtime environment structure */
struct s_runtime {
	struct {
		bool configured;
		enum e_systemmode startup_sysmode;	///< sysmode applied at startup. *REQUIRED*
		enum e_runmode startup_runmode;		///< if #startup_sysmode is #SYS_MANUAL, this runtime runmode will be applied and is *REQUIRED*.
		enum e_runmode startup_dhwmode;		///< if #startup_sysmode is #SYS_MANUAL, this runtime dhwmode will be applied and is *REQUIRED*
		const char * notifier;			///< notifier execvp()'d when alarms are logged. Arguments are individual alarm messages
	} set;
	struct {
		_Atomic enum e_systemmode systemmode;	///< current operation mode
		_Atomic enum e_runmode runmode;		///< CANNOT BE #RM_AUTO
		_Atomic enum e_runmode dhwmode;		///< CANNOT BE #RM_AUTO or #RM_DHWONLY
		atomic_bool stopdhw;			///< DHW kill switch: if true, all DHWTs will switch to RM_FROSTFREE
	} run;
	struct s_plant * restrict plant;	///< running plant
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

enum e_systemmode runtime_systemmode(void);
enum e_runmode runtime_runmode(void);
enum e_runmode runtime_dhwmode(void);
bool runtime_get_stopdhw(void);
int runtime_set_stopdhw(bool state);

#endif /* rwchcd_runtime_h */
