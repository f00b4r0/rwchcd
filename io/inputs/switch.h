//
//  io/inputs/switch.h
//  rwchcd
//
//  (C) 2023 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global switch interface API.
 */


#ifndef switch_h
#define switch_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "hw_backends/hardware.h"

/** Processing applied to multiple sources */
enum e_switch_op {
	S_OP_FIRST = 0,	///< use first value. Config `first`. *Default*
	S_OP_AND,	///< use logical AND of all available values. Config `and`
	S_OP_OR,	///< use logical OR of all available values. Config `or`
} ATTRPACK;

/** Missing source behavior */
enum e_switch_miss {
	S_MISS_FAIL = 0,///< fail if any underlying source cannot be read. Config `fail`. *Default*
	S_MISS_IGN,	///< ignore sources that cannot be read. If all sources cannot be read the switch will return an error. Config `ignore`. @note if #S_OP_FIRST is set, a basic failover system is created.
	S_MISS_IGNDEF,	///< Assign default value  `ignstate` to sources that cannot be read. Config `ignoredef`. @warning if #S_OP_FIRST is set, if the first source fails then the default value will be returned.
} ATTRPACK;

/** Software representation of a switch */
struct s_switch {
	struct {
		bool configured;	///< switch is configured
		bool ignstate;		///< state used for unavailable switches. *Optional*
		enum e_switch_op op;	///< operation performed on underlying sensors, see #e_switch_op. *Optional*, defaults to #S_OP_FIRST
		enum e_switch_miss missing;	///< missing/error source behavior see #e_switch_miss. *Optional*, defaults to #S_MISS_FAIL
		timekeep_t period;	///< update period for the reported value. *REQUIRED*. Also defines the time after which the value will be considered stale (4*period).
	} set;		///< settings
	struct {
		atomic_flag lock;	///< basic mutex to avoid multiple threads updating at the same time
		atomic_bool state;	///< current switch state
		atomic_bool error;	///< true if switch state is invalid
		_Atomic timekeep_t last_update;	///< last valid update
	} run;		///< private runtime
	uint_fast8_t num;		///< number of switches sources allocated. Max 256
	uint_fast8_t last;		///< last free source slot. if last == num, array is full.
	binid_t * list;			///< an ordered array of switches sources
	const char * restrict name;	///< @b unique user-defined name for the switch
};

int switch_get(struct s_switch * const s, bool * const state);
void switch_clear(struct s_switch * const s);

#endif /* switch_h */
