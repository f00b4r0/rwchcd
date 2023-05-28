//
//  io/inputs/switch.c
//  rwchcd
//
//  (C) 2023 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global switch interface implementation.
 *
 * This subsystem abstracts switch inputs, and uses a polling logic.
 * It allows for "meta-sensors" to be created: a single switch value can be the result of the processing of multiple sources, transparently for the end consumer.
 *
 * The switch implementation supports:
 * - Virtually unlimited number of underlying backend sources per switch
 * - Assigning an individual update period per switch
 * - Basic management of underlying sources possible error state:
 *   - Report failure if any source is in error state
 *   - Ignore all source errors
 *   - Assign default value to error sources
 * - Basic logic operations on underlying sources to compute the swtich final value:
 *   - Use first source value
 *   - AND/OR of all available source values
 * If "ignore all source errors" is set together with "use first source value", a simple failover mechanism is achieved
 * (the implementation will always return an error if no valid source is available).
 *
 * @note the implementation is thread-safe.
 */

#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>
#include <string.h>

#include "lib.h"
#include "timekeep.h"
#include "hw_backends/hardware.h"
#include "switch.h"
#include "alarms.h"

static inline int hardware_sensor_clone_switch(const binid_t swid, bool * const state)
{
	u_hw_in_value_t value;
	int ret;

	ret = hardware_input_value_get(swid, HW_INPUT_SWITCH, &value);

	*state = value.inswitch;

	return (ret);
}

#define hardware_sensor_clone_time(swid, clast)		hardware_input_time_get(swid, HW_INPUT_SWITCH, clast)

/**
 * Update a switch value.
 * This function will update a switch value if the last update time exceeds the set period.
 * A new value and timestamp will only be stored if source fetch completes without error.
 * Depending on the value of #s->set.missing, "without error" can have different meanings.
 * @param s the switch to update
 * @return exec status
 */
static int switch_update(struct s_switch * const s)
{
	int ret;
	uint_fast8_t i;
	bool sstate, new, gotone;
	const timekeep_t now = timekeep_now();
	timekeep_t tsens;

	if (unlikely(!s->set.configured))
		return (-ENOTCONFIGURED);

	// only skip run if we're under update period and we have a state (this handles init/failures)
	if ((now - (aler(&s->run.last_update)) < s->set.period) && aler(&s->run.state))
		return (ALL_OK);

	if (atomic_flag_test_and_set_explicit(&s->run.lock, memory_order_acquire))
		return (ALL_OK);	// someone else is already updating - NB: contention is NOT expected during init: assert(run.value is set) when it happens

	gotone = false;
	new = (S_OP_AND == s->set.op);	// if we are going to AND all values, start from a logical 1
	ret = -EGENERIC;
	for (i = 0; i < s->last; i++) {
		ret = hardware_sensor_clone_time(s->list[i], &tsens);
		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": hw clone time %d/%d returned (%d)", s->name, s->list[i].bid, s->list[i].inid, ret);
			switch (s->set.missing) {
				case S_MISS_IGN:
					continue;
				case S_MISS_IGNDEF:
					tsens = now;
					break;
				case S_MISS_FAIL:
				default:
					goto end;
			}
		}

		ret = hardware_sensor_clone_switch(s->list[i], &sstate);
		if (likely(ALL_OK == ret)) {
			// always weed out sensors for which the backend reports last update too far in the past (>4 periods).
			// while the loop executes, "now" can already be in the past => check for that
			if (unlikely((now - tsens) > (4 * s->set.period)) && timekeep_a_ge_b(now, tsens))
				ret = -ERSTALE;
		}

		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": hw clone switch %d/%d returned (%d)", s->name, s->list[i].bid, s->list[i].inid, ret);
			switch (s->set.missing) {
				case S_MISS_IGN:
					if (gotone)
						ret = ALL_OK;
					continue;
				case S_MISS_IGNDEF:
					sstate = s->set.ignstate;
					ret = ALL_OK;
					break;
				case S_MISS_FAIL:
				default:
					goto end;
			}
		}
		else
			gotone = true;

		switch (s->set.op) {
			default:
				dbgerr("\"%s\": invalid operation", s->name);
				ret = -EINVALID;
				// fallthrough
			case S_OP_FIRST:
				new = sstate;
				goto end;
			case S_OP_AND:
				new &= sstate;
				break;
			case S_OP_OR:
				new |= sstate;
				break;
		}
	}

end:
	// switch is updated if the above loop returns successfully
	if (likely(ALL_OK == ret)) {
		aser(&s->run.state, new);
		aser(&s->run.last_update, now);
		aser(&s->run.error, false);
	}
	else {
		// current state is untouched
		aser(&s->run.error, true);
		if (S_MISS_IGN != s->set.missing)	// don't alarm for "ignore" missing switches
			alarms_raise(ret, _("Switch \"%s\" invalid"), s->name);
	}

	atomic_flag_clear_explicit(&s->run.lock, memory_order_release);
	return (ret);
}

/**
 * Get a switch current value.
 * @param s pointer to switch to read from
 * @param sout pointer  to store the state value
 * @return exec status
 * @note side-effect: this function will update on-demand the switch value.
 */
int switch_get(struct s_switch * const s, bool * const sout)
{
	int ret;
	bool current, error;

	assert(s);

	ret = switch_update(s);
	if (ALL_OK != ret)
		return (ret);

	error = aler(&s->run.error);
	current = aler(&s->run.state);

	if (error)
		return (-EINVALID);

	if (sout)
		*sout = current;

	return (ALL_OK);
}

/**
 * Clear a switch allocated memory.
 * @param s the switch to clear.
 */
void switch_clear(struct s_switch * const s)
{
	if (!s)
		return;

	freeconst(s->name);
	freeconst(s->list);

	memset(s, 0x00, sizeof(*s));
}
