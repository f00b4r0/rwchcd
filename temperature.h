//
//  temperature.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global temperature interface API.
 */

#ifndef temperature_h
#define temperature_h

#include <stdatomic.h>

#include "rwchcd.h"


enum e_temp_op {
	T_OP_FIRST = 0,	///< use first value. Config `first`
	T_OP_MIN,	///< use min value. Config `min`
	T_OP_MAX,	///< use max value. Config `max`
};

enum e_temp_miss {
	T_MISS_FAIL = 0,///< fail if any underlying source cannot be read. Config `fail`
	T_MISS_IGN,	///< ignore sources that cannot be read. If all sources cannot be read the temperature will return an error. Config `ignore`. @note if #T_OP_FIRST is set, a basic failover system is created.
	T_MISS_IGNDEF,	///< Assign default value  `igntemp` to sources that cannot be read. Config `ignoredef`. @warning if #T_OP_FIRST is set, if the first source fails then the default value will be returned.
};

/** software representation of a temperature */
struct s_temperature {
	struct {
		bool configured;	///< sensor is configured
		timekeep_t period;	///< update period for the reported value. MANDATORY
		temp_t igntemp;		///< temperature used for unavailable sensors. OPTIONAL
		enum e_temp_op op;	///< operation performed on underlying sensors, see #e_temp_op. OPTIONAL, defaults to #T_OP_FIRST
		enum e_temp_miss missing;	///< missing/error source behavior see #e_temp_miss. OPTIONAL, defaults to #T_MISS_FAIL
	} set;		///< settings
	struct {
		atomic_flag lock;	///< basic mutex to avoid multiple threads updating at the same time
		_Atomic temp_t value;	///< current temperature value
		_Atomic timekeep_t last_update;	///< last valid update
	} run;		///< private runtime
	uint_fast8_t tnum;		///< number of temperature sources allocated. Max 256
	uint_fast8_t tlast;		///< last free source slot. if tlast == tnum, array is full.
	tempid_t * tlist;		///< an ordered array of temperature sources
	const char * restrict name;	///< @b unique user-defined name for the temperature
};

int temperature_get(struct s_temperature * const t, temp_t * const tout);
int temperature_time(struct s_temperature * const t, timekeep_t * const tstamp);
void temperature_clear(struct s_temperature * const t);

#endif /* temperature_h */
