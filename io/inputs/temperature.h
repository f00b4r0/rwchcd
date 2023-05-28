//
//  io/inputs/temperature.h
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
#include "hw_backends/hardware.h"

/** Processing applied to multiple sources */
enum e_temp_op {
	T_OP_FIRST = 0,	///< use first value. Config `first`. *Default*
	T_OP_MIN,	///< use min of all available values. Config `min`
	T_OP_MAX,	///< use max of all available values. Config `max`
} ATTRPACK;

/** Missing source behavior */
enum e_temp_miss {
	T_MISS_FAIL = 0,///< fail if any underlying source cannot be read. Config `fail`. *Default*
	T_MISS_IGN,	///< ignore sources that cannot be read. If all sources cannot be read the temperature will return an error. Config `ignore`. @note if #T_OP_FIRST is set, a basic failover system is created.
	T_MISS_IGNDEF,	///< Assign default value  `igntemp` to sources that cannot be read. Config `ignoredef`. @warning if #T_OP_FIRST is set, if the first source fails then the default value will be returned.
} ATTRPACK;

/** Software representation of a temperature */
struct s_temperature {
	struct {
		bool configured;	///< sensor is configured
		enum e_temp_op op;	///< operation performed on underlying sensors, see #e_temp_op. *Optional*, defaults to #T_OP_FIRST
		enum e_temp_miss missing;	///< missing/error source behavior see #e_temp_miss. *Optional*, defaults to #T_MISS_FAIL
		timekeep_t period;	///< update period for the reported value. *REQUIRED*. Also defines the time after which the value will be considered stale (4*period).
		temp_t igntemp;		///< temperature used for unavailable sensors. *Optional*
	} set;		///< settings
	struct {
		atomic_flag lock;	///< basic mutex to avoid multiple threads updating at the same time
		_Atomic temp_t value;	///< current temperature value
		_Atomic timekeep_t last_update;	///< last valid update
	} run;		///< private runtime
	uint_fast8_t num;		///< number of temperature sources allocated. Max 256
	uint_fast8_t last;		///< last free source slot. if last == num, array is full.
	binid_t * list;		///< an ordered array of temperature sources
	const char * restrict name;	///< @b unique user-defined name for the temperature
};

int temperature_get(struct s_temperature * const t, temp_t * const tout);
int temperature_time(struct s_temperature * const t, timekeep_t * const tstamp);
void temperature_clear(struct s_temperature * const t);

#endif /* temperature_h */
