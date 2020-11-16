//
//  io/inputs/temperature.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global temperatures interface implementation.
 *
 * This subsystem abstracts temperature inputs,
 * It allows for "meta-sensors" to be created: a single temperature value can be the result of the processing of multiple sources, transparently for the end consumer.
 *
 * The temperature implementation supports:
 * - Virtually unlimited number of underlying backend sources per temperature
 * - Assigning an individual update period per temperature
 * - Basic management of underlying sources possible error state:
 *   - Report failure if any source is in error state
 *   - Ignore all source errors
 *   - Assign default value to error sources
 * - Basic arithmetic operations on underlying sources to compute the temperature final value:
 *   - Use first source value
 *   - Min/Max of all available source values
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
#include "temperature.h"

static inline int hardware_sensor_clone_temp(const binid_t tempid, temp_t * const ctemp)
{
	u_hw_in_value_t value;
	int ret;

	ret = hardware_input_value_get(tempid, HW_INPUT_TEMP, &value);

	*ctemp = value.temperature;

	return (ret);
}

#define hardware_sensor_clone_time(tempid, clast)	hardware_input_time_get(tempid, HW_INPUT_TEMP, clast)

/**
 * Update a temperature value.
 * This function will update a temperature value if the last update time exceeds the set period.
 * A new value and timestamp will only be stored if source fetch completes without error.
 * Depending on the value of #t->set.missing, "without error" can have different meanings.
 * @param t the temperature to update
 * @return exec status
 */
static int temperature_update(struct s_temperature * const t)
{
	int ret;
	uint_fast8_t i;
	temp_t stemp, new;
	const timekeep_t now = timekeep_now();
	timekeep_t tsens;

	if (unlikely(!t->set.configured))
		return (-ENOTCONFIGURED);

	// only skip run if we're under update period and we have a value (this handles init)
	if ((now - (aler(&t->run.last_update)) < t->set.period) && aler(&t->run.value))
		return (ALL_OK);

	if (atomic_flag_test_and_set_explicit(&t->run.lock, memory_order_acquire))
		return (ALL_OK);	// someone else is already updating - NB: contention is NOT expected during init: assert(run.value is set) when it happens

	new = 0;
	ret = -EGENERIC;
	for (i = 0; i < t->tlast; i++) {
		ret = hardware_sensor_clone_time(t->tlist[i], &tsens);
		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": hw clone time %d/%d returned (%d)", t->name, t->tlist[i].bid, t->tlist[i].inid, ret);
			switch (t->set.missing) {
				case T_MISS_IGN:
					continue;
				case T_MISS_IGNDEF:
					tsens = now;
					break;
				case T_MISS_FAIL:
				default:
					goto end;
			}
		}


		ret = hardware_sensor_clone_temp(t->tlist[i], &stemp);
		if (likely(ALL_OK == ret)) {
			// always weed out sensors for which the backend reports last update too far in the past (>4 periods).
			// while the loop executes, "now" can already be in the past => check for that
			if (unlikely((now - tsens) > (4 * t->set.period)) && timekeep_a_ge_b(now, tsens))
				ret = -ERSTALE;
			// treat invalid value as invalid even if the backend doesn't say so
			if (unlikely(validate_temp(stemp)))
				ret = -EINVALID;
		}

		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": hw clone temp %d/%d returned (%d)", t->name, t->tlist[i].bid, t->tlist[i].inid, ret);
			switch (t->set.missing) {
				case T_MISS_IGN:
					continue;
				case T_MISS_IGNDEF:
					stemp = t->set.igntemp;
					ret = ALL_OK;
					break;
				case T_MISS_FAIL:
				default:
					goto end;
			}
		}

		if (!new)
			new = stemp;

		switch (t->set.op) {
			default:
				dbgerr("\"%s\": invalid operation", t->name);
				ret = -EINVALID;
				// fallthrough
			case T_OP_FIRST:
				goto end;	// assignement handled by the if (!new)
			case T_OP_MIN:
				new = (new < stemp) ? new : stemp;
				break;
			case T_OP_MAX:
				new = (new > stemp) ? new : stemp;
				break;
		}
	}

end:
	// temperature is updated if the above loop returns successfully
	if (likely(ALL_OK == ret)) {
		aser(&t->run.value, new);
		aser(&t->run.last_update, now);
	}
	else
		aser(&t->run.value, TEMPINVALID);

	atomic_flag_clear_explicit(&t->run.lock, memory_order_release);
	return (ret);
}

/**
 * Get a temperature current value.
 * @param t pointer to temperature to read from
 * @param tout pointer  to store the temperature value
 * @return exec status
 * @note side-effect: this function will update on-demand the temperature value.
 */
int temperature_get(struct s_temperature * const t, temp_t * const tout)
{
	int ret;
	temp_t current;

	assert(t);

	ret = temperature_update(t);
	if (ALL_OK != ret)
		return (ret);

	current = aler(&t->run.value);

	if (tout)
		*tout = current;

	if (!current)
		return (-EINVALID);

	return (ALL_OK);
}

/**
 * Get a temperature last update time.
 * @param t pointer to temperature to read from
 * @param tstamp pointer  to store the time value
 * @return exec status
 */
int temperature_time(struct s_temperature * const t, timekeep_t * const tstamp)
{
	assert(t);

	if (tstamp)
		*tstamp = aler(&t->run.last_update);

	return (ALL_OK);
}

/**
 * Clear a temperature allocated memory.
 * @param t the temperature to clear.
 */
void temperature_clear(struct s_temperature * const t)
{
	if (!t)
		return;

	free((void *)t->name);
	free((void *)t->tlist);

	memset(t, 0x00, sizeof(*t));
}
