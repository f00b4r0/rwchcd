//
//  io/outputs/relay.c
//  rwchcd
//
//  (C) 2020-2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global relay interface implementation.
 *
 * This subsystem abstracts relay outputs,
 * It allows for "meta-relays" to be created: a software relay can be control multiple backend targets, transparently for the end consumer.
 *
 * The relay implementation supports:
 * - Virtually unlimited number of underlying backend targets per relay
 * - Basic management of underlying targets possible error states:
 *   - Report failure if any target is in error state
 *   - Ignore all target errors
 * - Basic logic operations on underlying targets:
 *   - Stop at first non-error target
 *   - Effect all targets
 * If "stop at first non-error target" is set together with "ignore all target errors", a simple failover mechanism is achieved
 * (the first working target is controlled, the implementation will always report an error if no working target is available).
 *
 * Basic accounting is provided by this abstraction layer. Specifically, total on/off time as well as total cycles are available.
 * The metrics only reflect the "software view" of outputs, they do not account for how/when backends actually enact the requested states.
 * Furthermore, these metrics - while always increasing (until overflow) - are only guaranteed to do so within the current execution of the main program.
 * In other words, these counters will reset when the program is restarted. This is not a problem for most time-series databases.
 *
 * @note the implementation is thread-safe.
 */

#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

#include "timekeep.h"
#include "hw_backends/hardware.h"
#include "relay.h"

/**
 * Grab a relay for exclusive use.
 * @param r the relay to claim
 * @return exec status
 */
int relay_grab(struct s_relay * const r)
{
	bool prev;

	assert(r);

	if (unlikely(!r->set.configured))
		return (-ENOTCONFIGURED);

	prev = atomic_flag_test_and_set(&r->run.grabbed);

	if (prev)
		return (-EEXISTS);
	else
		return (ALL_OK);
}

/**
 * Thaw a relay that was previously grabbed.
 * @param r the relay to claim
 * @return exec status
 */
int relay_thaw(struct s_relay * const r)
{
	assert(r);

	if (unlikely(!r->set.configured))
		return (-ENOTCONFIGURED);

	atomic_flag_clear(&r->run.grabbed);

	return (ALL_OK);
}

static inline int hardware_relay_set_state(const boutid_t relid, const bool turn_on)
{
	const u_hw_out_state_t state = { turn_on };
	return (hardware_output_state_set(relid, HW_OUTPUT_RELAY, &state));
}

/**
 * Update relay accounting.
 * @param r the target relay
 * @param turn_on the new relay state
 */
static void relay_update(struct s_relay * const r, const bool turn_on)
{
	timekeep_t state_time, now;

	// update state time counter
	now = timekeep_now();
	state_time = now - r->run.state_since;

	// we are only called on actual state changes, with lock held
	if (turn_on) {	// relay is currently off => turn on
		aser(&r->run.cycles, aler(&r->run.cycles) + 1);	// increment cycle count
		r->run.off_totsecs += (unsigned)timekeep_tk_to_sec(state_time);
	}
	else {				// relay is currently on => turn off
		r->run.on_totsecs += (unsigned)timekeep_tk_to_sec(state_time);
	}
	r->run.state_since = now;
}

/**
 * Set an output relay state.
 * This function will request target relays to update according to the #turn_on parameter.
 * This function performs a simple check and only propagates the request to the backends if the requested state differs from the last known state.
 * The new state will only be stored if target request completes without error.
 * Depending on the value of #r->set.missing, "without error" can have different meanings.
 * @param r the output relay to act on
 * @param turn_on the requested state for the relay
 * @return exec status
 * @note this function spinlocks
 * @warning this function assumes that a given software relay has only @b one user that can set its state (as enforced by relay_grab()), and that this user cannot send concurrent requests.
 * Using this assumption enables to move the check for current state outside of the lock to spare the overhead when the same order is repeated.
 */
int relay_state_set(struct s_relay * const r, const bool turn_on)
{
	int ret;
	uint_fast8_t i;
	bool state;

	assert(r);

	if (unlikely(!r->set.configured))
		return (-ENOTCONFIGURED);

	// we can check here based on the assumption described in the top comment.
	state = aler(&r->run.turn_on);
	if (turn_on == state)
		return (ALL_OK);

	// we must ensure all requests get through. Spinlock if someone else is touching the target.
	// Note: based on top comment assumption, the only possible contention here is through the accounting access routines
	while (unlikely(atomic_flag_test_and_set_explicit(&r->run.lock, memory_order_acquire)));

	// a change is needed, let's dive in
	ret = -EGENERIC;
	for (i = 0; i < r->rlast; i++) {
		ret = hardware_relay_set_state(r->rlist[i], turn_on);
		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": hw relay set state %d/%d returned (%d)", r->name, r->rlist[i].bid, r->rlist[i].outid, ret);
			switch (r->set.missing) {
				case R_MISS_IGN:
					continue;
				case R_MISS_FAIL:
				default:
					goto end;
			}
		}

		switch (r->set.op) {
			default:
				dbgerr("\"%s\": invalid operation", r->name);
				ret = -EINVALID;
				// fallthrough
			case R_OP_FIRST:
				goto end;
			case R_OP_ALL:
				// nothing
				break;
		}
	}

end:
	// at least one good relay must be reached for the value to be updated
	if (likely(ALL_OK == ret)) {
		aser(&r->run.turn_on, turn_on);
		relay_update(r, turn_on);
	}

	atomic_flag_clear_explicit(&r->run.lock, memory_order_release);
	return (ret);
}

/**
 * Return an output relay state.
 * This function returns the "software view" of the state of the relay.
 * @param r the output relay to read from
 * @return relay state or error
 * @note this function does @b not query the backends. lockless
 */
int relay_state_get(const struct s_relay * const r)
{
	assert(r);

	if (unlikely(!r->set.configured))
		return (-ENOTCONFIGURED);

	return (aler(&r->run.turn_on));
}

/**
 * Return total number of seconds the relay was ON.
 * @note spinlocks
 */
uint32_t relay_acct_ontotsec_get(struct s_relay * const r)
{
	uint32_t total, offset = 0;

	while (unlikely(atomic_flag_test_and_set_explicit(&r->run.lock, memory_order_acquire)))
		timekeep_usleep(500);	// yield

	if (aler(&r->run.turn_on))
		offset = (unsigned)timekeep_tk_to_sec(timekeep_now() - r->run.state_since);

	total = r->run.on_totsecs + offset;

	atomic_flag_clear_explicit(&r->run.lock, memory_order_release);

	return (total);
}

/**
 * Return total number of seconds the relay was OFF.
 * @note spinlocks
 */
uint32_t relay_acct_offtotsec_get(struct s_relay * const r)
{
	uint32_t total, offset = 0;

	while (unlikely(atomic_flag_test_and_set_explicit(&r->run.lock, memory_order_acquire)))
		timekeep_usleep(500);	// yield

	if (!aler(&r->run.turn_on))
		offset = (unsigned)timekeep_tk_to_sec(timekeep_now() - r->run.state_since);

	total = r->run.off_totsecs + offset;

	atomic_flag_clear_explicit(&r->run.lock, memory_order_release);

	return (total);
}

/**
 * Return total number of cycles performed by the relay.
 * @note lockless
 */
uint32_t relay_acct_cycles_get(const struct s_relay * const r)
{
	return (aler(&r->run.cycles));
}

/**
 * Clear a relay allocated memory.
 * @param r the relay to clear.
 */
void relay_clear(struct s_relay * const r)
{
	if (!r)
		return;

	freeconst(r->name);
	freeconst(r->rlist);

	memset(r, 0x00, sizeof(*r));
}
