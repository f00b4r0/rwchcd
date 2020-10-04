//
//  relay.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global relay interface API.
 */

#ifndef relay_h
#define relay_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "hardware.h"


enum e_relay_op {
	R_OP_FIRST = 0,	///< Control first relay. Config `first`
	R_OP_ALL,	///< Control all relays. Config `all`
};

enum e_relay_miss {
	R_MISS_FAIL = 0,///< fail if any underlying target cannot be reached. Config `fail`
	R_MISS_IGN,	///< ignore targets that cannot be reached. If all sources cannot be read the relay will return an error. Config `ignore`. @note if #R_OP_FIRST is set, a basic failover system is created (where the first @b working relay is controlled).
};

/** software representation of a relay */
struct s_relay {
	struct {
		bool configured;	///< sensor is configured
		enum e_relay_op op;	///< operation performed on underlying relays, see #e_relay_op. OPTIONAL, defaults to R_OP_FIRST
		enum e_relay_miss missing;	///< missing relay behavior see #e_relay_miss. OPTIONAL, defaults to R_MISS_FAIL
	} set;		///< settings (externally set)
	struct {
		atomic_flag lock;	///< basic spinlock to avoid multiple threads updating at the same time
		_Atomic bool turn_on;
	} run;		///< private runtime (internally handled)
	uint_fast8_t rnum;		///< number of relay targets allocated. Max 256
	uint_fast8_t rlast;		///< last free target slot. if rlast == rnum, array is full.
	relid_t * rlist;		///< an ordered array of relay targets
	const char * restrict name;	///< @b unique user-defined name for the relay
};

int relay_state_set(struct s_relay * const r, const bool turn_on);
int relay_state_get(const struct s_relay * const r);
void relay_clear(struct s_relay * const r);

#endif /* relay_h */
