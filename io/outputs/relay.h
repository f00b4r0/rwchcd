//
//  io/outputs/relay.h
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
#include "hw_backends/hardware.h"


/** Processing applied to multiple targets */
enum e_relay_op {
	R_OP_FIRST = 0,	///< Control first relay. Config `first`. *Default*
	R_OP_ALL,	///< Control all relays. Config `all`
} ATTRPACK;

/** Missing target behavior */
enum e_relay_miss {
	R_MISS_FAIL = 0,///< fail if any underlying target cannot be reached. Config `fail`. *Default*
	R_MISS_IGN,	///< ignore targets that cannot be reached. If all sources cannot be read the relay will return an error. Config `ignore`. @note if #R_OP_FIRST is set, a basic failover system is created (where the first @b working relay is controlled).
} ATTRPACK;

/** Software representation of a relay */
struct s_relay {
	struct {
		bool configured;	///< sensor is configured
		enum e_relay_op op;	///< operation performed on underlying relays, see #e_relay_op. *Optional*, defaults to R_OP_FIRST
		enum e_relay_miss missing;	///< missing relay behavior see #e_relay_miss. *Optional*, defaults to R_MISS_FAIL
	} set;		///< settings (externally set)
	struct {
		atomic_flag grabbed;	///< relay has been claimed by an active user (that will set its state)
		atomic_flag lock;	///< basic spinlock to avoid multiple threads updating at the same time
		_Atomic bool turn_on;
		_Atomic uint32_t cycles;	///< number of power cycles since start
		// these variables are accessed under lock
		uint32_t on_totsecs;	///< total seconds spent in on state since start (updated at state change only)
		uint32_t off_totsecs;	///< total seconds spent in off state since start (updated at state change only)
		timekeep_t state_since;	///< last time state changed
	} run;		///< private runtime (internally handled)
	uint_fast8_t rnum;		///< number of relay targets allocated. Max 256
	uint_fast8_t rlast;		///< last free target slot. if rlast == rnum, array is full.
	boutid_t * rlist;		///< an ordered array of relay targets
	const char * restrict name;	///< @b unique user-defined name for the relay
};

int relay_grab(struct s_relay * const r);
int relay_thaw(struct s_relay * const r);
int relay_state_set(struct s_relay * const r, const bool turn_on);
int relay_state_get(const struct s_relay * const r);
uint32_t relay_acct_ontotsec_get(struct s_relay * const r);
uint32_t relay_acct_offtotsec_get(struct s_relay * const r);
uint32_t relay_acct_cycles_get(const struct s_relay * const r);
void relay_clear(struct s_relay * const r);

#endif /* relay_h */
