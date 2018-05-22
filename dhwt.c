//
//  dhwt.c
//  rwchcd
//
//  (C) 2017-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "pump.h"
#include "dhwt.h"
#include "hardware.h"
#include "lib.h"
#include "runtime.h"
#include "config.h"

/**
 * Create a dhwt
 * @return the newly created dhwt or NULL
 */
struct s_dhw_tank * dhwt_new(void)
{
	struct s_dhw_tank * const dhwt = calloc(1, sizeof(*dhwt));
	return (dhwt);
}

/**
 * Put dhwt online.
 * Perform all necessary actions to prepare the dhwt for service but
 * DO NOT MARK IT AS ONLINE.
 * @param dhwt target dhwt
 * @return exec status
 */
int dhwt_online(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	temp_t temp;
	int ret = -EGENERIC;

	assert(runtime);

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are set
	ret = hardware_sensor_clone_time(dhwt->set.tid_bottom, NULL);
	if (ALL_OK != ret)
		ret = hardware_sensor_clone_time(dhwt->set.tid_top, NULL);
	if (ret)
		goto out;

	// limit_tmin must be > 0C
	temp = SETorDEF(dhwt->set.params.limit_tmin, runtime->config->def_dhwt.limit_tmin);
	if (temp <= celsius_to_temp(0))
		ret = -EMISCONFIGURED;

	// limit_tmax must be > limit_tmin
	if (SETorDEF(dhwt->set.params.limit_tmax, runtime->config->def_dhwt.limit_tmax) <= temp)
		ret = -EMISCONFIGURED;

	// hysteresis must be > 0K
	if (SETorDEF(dhwt->set.params.hysteresis, runtime->config->def_dhwt.hysteresis) <= 0)
		ret = -EMISCONFIGURED;

	// t_frostfree must be > 0C
	temp = SETorDEF(dhwt->set.params.t_frostfree, runtime->config->def_dhwt.t_frostfree);
	if (temp <= celsius_to_temp(0))
		ret = -EMISCONFIGURED;

	// t_comfort must be > t_frostfree
	if (SETorDEF(dhwt->set.params.t_comfort, runtime->config->def_dhwt.t_comfort) < temp)
		ret = -EMISCONFIGURED;

	// t_eco must be > t_frostfree
	if (SETorDEF(dhwt->set.params.t_eco, runtime->config->def_dhwt.t_eco) < temp)
		ret = -EMISCONFIGURED;

	// if pumps exist check they're correctly configured
	if (dhwt->feedpump && !dhwt->feedpump->set.configured) {
		dbgerr("\"%s\": feedpump \"%s\" not configured", dhwt->name, dhwt->feedpump->name);
		ret = -EMISCONFIGURED;
	}

	if (dhwt->recyclepump && !dhwt->recyclepump->set.configured) {
		dbgerr("\"%s\": recyclepump \"%s\" not configured", dhwt->name, dhwt->recyclepump->name);
		ret = -EMISCONFIGURED;
	}

out:
	return (ret);
}

/**
 * Flag actuators currently used.
 * This function is necessary to ensure proper behavior of the summer maintenance
 * system:
 * - When the DHWT is in active use (ECO/COMFORT) then the related actuators
 *   are flagged in use.
 * - When the DHWT is offline or in FROSTFREE then the related actuators are
 *   unflagged. This works because the summer maintenance can only run when
 *   frost condition is @b GUARANTEED not to happen.
 * @note the feedpump is @b NOT unflagged when running electric to avoid sending
 * cold water into the feed circuit. Thus the feedpump cannot be "summer maintained"
 * when the DHWT is running electric.
 * @param dhwt target dhwt
 * @param active flag status
 */
static inline void dhwt_actuator_use(struct s_dhw_tank * const dhwt, bool active)
{
	assert(dhwt);

	if (dhwt->feedpump)
		dhwt->feedpump->run.dwht_use = active;

	if (dhwt->recyclepump)
		dhwt->recyclepump->run.dwht_use = active;
}

/**
 * Put dhwt offline.
 * Perform all necessary actions to completely shut down the dhwt but
 * DO NOT MARK IT AS OFFLINE.
 * @param dhwt target dhwt
 * @return error status
 */
int dhwt_offline(struct s_dhw_tank * const dhwt)
{
	bool online;

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	// clear runtime data while preserving online state
	online = dhwt->run.online;
	memset(&dhwt->run, 0x0, sizeof(dhwt->run));
	dhwt->run.online = online;

	dhwt_actuator_use(dhwt, false);

	if (dhwt->feedpump)
		pump_offline(dhwt->feedpump);

	if (dhwt->recyclepump)
		pump_offline(dhwt->recyclepump);

	hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);

	dhwt->run.runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * DHWT failsafe routine.
 * By default we stop all pumps and electric self heater. If configured for
 * electric failover the self-heater is turned on unconditionnally.
 * The major inconvenient here is that this failsafe mode COULD provoke a DHWT
 * freeze in the most adverse conditions.
 * @warning DHWT could freeze - TODO: needs review
 * @param dhwt target dhwt
 */
static void dhwt_failsafe(struct s_dhw_tank * restrict const dhwt)
{
	int ret;

	assert(dhwt);

	if (dhwt->feedpump)
		pump_set_state(dhwt->feedpump, OFF, FORCE);
	if (dhwt->recyclepump)
		pump_set_state(dhwt->recyclepump, OFF, FORCE);
	ret = hardware_relay_set_state(dhwt->set.rid_selfheater, dhwt->set.electric_failover ? ON : OFF, 0);
	if (ALL_OK == ret)
		dhwt->run.electric_mode = dhwt->set.electric_failover;
}

/**
 * DHWT control loop.
 * Controls the dhwt's elements to achieve the desired target temperature.
 * If charge time exceeds the limit, the DHWT will be stopped for the duration
 * of the set limit.
 * Due to implementation in dhwt_failsafe() the DHWT can be configured to operate
 * purely on electric heating in the event of sensor failure, but this is still
 * considered a degraded operation mode and it will be reported as an error.
 * @param dhwt target dhwt
 * @return error status
 * @bug discharge protection might fail if the input sensor needs water flow
 * in the feedpump. The solution to this is to implement a fallback to an upstream
 * temperature (e.g. the heatsource's).
 */
int dhwt_run(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	temp_t water_temp, top_temp, bottom_temp, curr_temp, wintmax, trip_temp;
	bool valid_ttop = false, valid_tbottom = false, test;
	const time_t now = time(NULL);
	time_t limit;
	int ret;

	assert(runtime);

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->run.online)	/// implies set.configured == true
		return (-EOFFLINE);

	switch (dhwt->run.runmode) {
		case RM_OFF:
			return (dhwt_offline(dhwt));
		case RM_COMFORT:
		case RM_ECO:
			dhwt_actuator_use(dhwt, true);
			break;
		case RM_FROSTFREE:
			dhwt_actuator_use(dhwt, false);
			break;
		case RM_TEST:
			if (dhwt->feedpump)
				pump_set_state(dhwt->feedpump, ON, FORCE);
			if (dhwt->recyclepump)
				pump_set_state(dhwt->recyclepump, ON, FORCE);
			hardware_relay_set_state(dhwt->set.rid_selfheater, ON, 0);
			return (ALL_OK);
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active

	// check which sensors are available
	ret = hardware_sensor_clone_temp(dhwt->set.tid_bottom, &bottom_temp);
	if (ALL_OK == ret)
		valid_tbottom = true;
	ret = hardware_sensor_clone_temp(dhwt->set.tid_top, &top_temp);
	if (ALL_OK == ret)
		valid_ttop = true;

	// no sensor available, give up
	if (!valid_tbottom && !valid_ttop) {
		dhwt_failsafe(dhwt);
		return (ret);	// return last error
	}

	// We're good to go

	dbgmsg("\"%s\": on: %d, mode_since: %ld, tg_t: %.1f, bot_t: %.1f, top_t: %.1f",
	       dhwt->name, dhwt->run.charge_on, dhwt->run.mode_since, temp_to_celsius(dhwt->run.target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));

	// handle recycle loop
	if (dhwt->recyclepump) {
		if (dhwt->run.recycle_on)
			ret = pump_set_state(dhwt->recyclepump, ON, NOFORCE);
		else
			ret = pump_set_state(dhwt->recyclepump, OFF, NOFORCE);

		if (ALL_OK != ret)	// this is a non-critical error, keep going
			dbgerr("\"%s\": failed to set recyclepump \"%s\" state (%d)", dhwt->name, dhwt->recyclepump->name, ret);
	}

	/* handle heat charge - NOTE we enforce sensor position, it SEEMS desirable
	 apply hysteresis on logic: trip at target - hysteresis (preferably on bottom sensor),
	 untrip at target (preferably on top sensor). */
	if (!dhwt->run.charge_on) {	// no charge in progress
					// in non-electric mode: prevent charge "pumping", enforce delay between charges
		if (!dhwt->run.electric_mode) {
			limit = SETorDEF(dhwt->set.params.limit_chargetime, runtime->config->def_dhwt.limit_chargetime);
			if (dhwt->run.charge_overtime) {
				if (limit && ((now - dhwt->run.mode_since) <= limit))
					return (ALL_OK); // no further processing, must wait
				else
					dhwt->run.charge_overtime = false;	// reset status
			}
		}

		if (valid_tbottom)	// prefer bottom temp if available (trip charge when bottom is cold)
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;

		// set trip point to (target temp - hysteresis)
		if (dhwt->run.force_on)
			trip_temp = dhwt->run.target_temp - deltaK_to_temp(1);	// if forced charge, force hysteresis at 1K
		else
			trip_temp = dhwt->run.target_temp - SETorDEF(dhwt->set.params.hysteresis, runtime->config->def_dhwt.hysteresis);

		// trip condition
		if (curr_temp < trip_temp) {
			if (runtime->plant_could_sleep) {
				// the plant is sleeping and we have a configured self heater: use it
				ret = hardware_relay_set_state(dhwt->set.rid_selfheater, ON, 0);
				if (ALL_OK == ret)
					dhwt->run.electric_mode = true;
			}
			else {	// run from plant heat source
				dhwt->run.electric_mode = false;
				// calculate necessary water feed temp: target tank temp + offset
				water_temp = dhwt->run.target_temp + SETorDEF(dhwt->set.params.temp_inoffset, runtime->config->def_dhwt.temp_inoffset);

				// enforce limits
				wintmax = SETorDEF(dhwt->set.params.limit_wintmax, runtime->config->def_dhwt.limit_wintmax);
				if (water_temp > wintmax)
					water_temp = wintmax;

				// apply heat request
				dhwt->run.heat_request = water_temp;
			}

			// mark heating in progress
			dhwt->run.charge_on = true;
			dhwt->run.mode_since = now;
		}
	}
	else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
		if (valid_ttop)	// prefer top temp if available (untrip charge when top is hot)
			curr_temp = top_temp;
		else
			curr_temp = bottom_temp;

		// untrip conditions
		test = false;

		// in non-electric mode and no legionella charge: if heating gone overtime, untrip
		if (!dhwt->run.electric_mode && !dhwt->run.legionella_on) {
			limit = SETorDEF(dhwt->set.params.limit_chargetime, runtime->config->def_dhwt.limit_chargetime);
			if ((limit) && ((now - dhwt->run.mode_since) > limit)) {
				test = true;
				dhwt->run.charge_overtime = true;
			}
		}

		// if heating in progress, untrip at target temp
		if (curr_temp >= dhwt->run.target_temp)
			test = true;

		// stop all heat input (ensures they're all off at switchover)
		if (test) {
			// stop self-heater (if any)
			hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);

			// clear heat request
			dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;

			// untrip force charge: force can run only once
			dhwt->run.force_on = false;

			// mark heating as done
			dhwt->run.legionella_on = false;
			dhwt->run.charge_on = false;
			dhwt->run.mode_since = now;
		}
	}

	ret = ALL_OK;

	// handle feedpump - outside of the trigger since we need to manage inlet temp
	if (dhwt->feedpump) {
		if (dhwt->run.charge_on && !dhwt->run.electric_mode) {	// on heatsource charge
									// if available, test for inlet water temp
			ret = hardware_sensor_clone_temp(dhwt->set.tid_win, &water_temp);	// XXX REVIEW: if this sensor relies on pump running for accurate read, then this can be a problem
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is < dhwt current temp, stop the pump
				if (water_temp < curr_temp)
					ret = pump_set_state(dhwt->feedpump, OFF, FORCE);
				else if (water_temp >= (curr_temp + deltaK_to_temp(1)))	// 1K hysteresis
					ret = pump_set_state(dhwt->feedpump, ON, NOFORCE);
			}
			else
				ret = pump_set_state(dhwt->feedpump, ON, NOFORCE);	// if sensor fails, turn on the pump unconditionally during heatsource charge
		}
		else {				// no charge or electric charge
			test = FORCE;	// by default, force feedpump immediate turn off

			// if available, test for inlet water temp
			ret = hardware_sensor_clone_temp(dhwt->set.tid_win, &water_temp);
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is > dhwt current temp, we can apply cooldown
				if (water_temp > curr_temp)
					test = NOFORCE;
			}

			// turn off pump with conditional cooldown
			ret = pump_set_state(dhwt->feedpump, OFF, test);
		}

		if (ALL_OK != ret)
			dbgerr("\"%s\": failed to set feedpump \"%s\" state (%d)", dhwt->name, dhwt->feedpump->name, ret);
	}

	return (ret);
}

/**
 * DHWT destructor.
 * Frees all dhwt-local resources
 * @param dhwt the dhwt to delete
 */
void dhwt_del(struct s_dhw_tank * restrict dhwt)
{
	if (!dhwt)
		return;

	free(dhwt->name);
	dhwt->name = NULL;

	free(dhwt);
}
