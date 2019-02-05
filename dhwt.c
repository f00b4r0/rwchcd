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
 * Perform all necessary actions to prepare the dhwt for service and
 * mark it as online.
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
	if (dhwt->pump_feed && !dhwt->pump_feed->set.configured) {
		dbgerr("\"%s\": pump_feed \"%s\" not configured", dhwt->name, dhwt->pump_feed->name);
		ret = -EMISCONFIGURED;
	}

	if (dhwt->pump_recycle && !dhwt->pump_recycle->set.configured) {
		dbgerr("\"%s\": pump_recycle \"%s\" not configured", dhwt->name, dhwt->pump_recycle->name);
		ret = -EMISCONFIGURED;
	}

	if (ALL_OK == ret)
		dhwt->run.online = true;

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
 * @note the pump_feed is @b NOT unflagged when running electric to avoid sending
 * cold water into the feed circuit. Thus the pump_feed cannot be "summer maintained"
 * when the DHWT is running electric.
 * @param dhwt target dhwt
 * @param active flag status
 */
static inline void dhwt_actuator_use(struct s_dhw_tank * const dhwt, bool active)
{
	assert(dhwt);

	if (dhwt->pump_feed)
		dhwt->pump_feed->run.dwht_use = active;

	if (dhwt->pump_recycle)
		dhwt->pump_recycle->run.dwht_use = active;
}

/**
 * Put dhwt offline.
 * Perform all necessary actions to completely shut down the dhwt.
 * @param dhwt target dhwt
 * @return error status
 */
int dhwt_shutdown(struct s_dhw_tank * const dhwt)
{
	assert(dhwt);
	assert(dhwt->set.configured);

	if (!dhwt->run.active)
		return (ALL_OK);

	// clear runtime data while preserving online state
	dhwt->run.charge_on = false;
	dhwt->run.recycle_on = false;
	dhwt->run.force_on = false;
	//dhwt->run.legionella_on = false;
	dhwt->run.charge_overtime = false;
	dhwt->run.mode_since = 0;	// XXX
	dhwt->run.charge_yday = 0;	// XXX

	dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	dhwt->run.target_temp = 0;

	dhwt_actuator_use(dhwt, false);

	if (dhwt->pump_feed)
		pump_shutdown(dhwt->pump_feed);

	if (dhwt->pump_recycle)
		pump_shutdown(dhwt->pump_recycle);

	hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);

	dhwt->run.active = false;

	return (ALL_OK);
}

/**
 * Put dhwt offline.
 * Perform all necessary actions to completely shut down the dhwt and
 * mark it as offline.
 * @param dhwt target dhwt
 * @return error status
 */
int dhwt_offline(struct s_dhw_tank * const dhwt)
{
	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	dhwt_shutdown(dhwt);

	memset(&dhwt->run, 0x0, sizeof(dhwt->run));
	//dhwt->run.runmode = RM_OFF;	// handled by memset
	//dhwt->run.online = false;	// handled by memset

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

	dbgerr("\"%s\": failsafe mode!", dhwt->name);

	if (dhwt->pump_feed)
		pump_set_state(dhwt->pump_feed, OFF, FORCE);
	if (dhwt->pump_recycle)
		pump_set_state(dhwt->pump_recycle, OFF, FORCE);
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
 * in the pump_feed. The solution to this is to implement a fallback to an upstream
 * temperature (e.g. the heatsource's).
 */
int dhwt_run(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	temp_t water_temp, top_temp, bottom_temp, curr_temp, wintmax, trip_temp;
	bool valid_ttop = false, valid_tbottom = false, test;
	const timekeep_t now = timekeep_now();
	timekeep_t limit;
	int ret;

	assert(runtime);

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->run.online)	/// implies set.configured == true
		return (-EOFFLINE);

	switch (dhwt->run.runmode) {
		case RM_OFF:
			return (dhwt_shutdown(dhwt));
		case RM_COMFORT:
		case RM_ECO:
			dhwt_actuator_use(dhwt, true);
			break;
		case RM_FROSTFREE:
			dhwt_actuator_use(dhwt, false);
			break;
		case RM_TEST:
			dhwt->run.active = true;
			if (dhwt->pump_feed)
				pump_set_state(dhwt->pump_feed, ON, FORCE);
			if (dhwt->pump_recycle)
				pump_set_state(dhwt->pump_recycle, ON, FORCE);
			hardware_relay_set_state(dhwt->set.rid_selfheater, ON, 0);
			return (ALL_OK);
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active
	dhwt->run.active = true;

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
	       dhwt->name, dhwt->run.charge_on, timekeep_tk_to_sec(dhwt->run.mode_since), temp_to_celsius(dhwt->run.target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));

	// handle recycle loop
	if (dhwt->pump_recycle) {
		if (dhwt->run.recycle_on)
			ret = pump_set_state(dhwt->pump_recycle, ON, NOFORCE);
		else
			ret = pump_set_state(dhwt->pump_recycle, OFF, NOFORCE);

		if (ALL_OK != ret)	// this is a non-critical error, keep going
			dbgerr("\"%s\": failed to set pump_recycle \"%s\" state (%d)", dhwt->name, dhwt->pump_recycle->name, ret);
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
			if (dhwt->pdata->plant_could_sleep) {
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

	// handle pump_feed - outside of the trigger since we need to manage inlet temp
	if (dhwt->pump_feed) {
		if (dhwt->run.charge_on && !dhwt->run.electric_mode) {	// on heatsource charge
									// if available, test for inlet water temp
			ret = hardware_sensor_clone_temp(dhwt->set.tid_win, &water_temp);	// XXX REVIEW: if this sensor relies on pump running for accurate read, then this can be a problem
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is < dhwt current temp, stop the pump
				if (water_temp < curr_temp)
					ret = pump_set_state(dhwt->pump_feed, OFF, FORCE);
				else if (water_temp >= (curr_temp + deltaK_to_temp(1)))	// 1K hysteresis
					ret = pump_set_state(dhwt->pump_feed, ON, NOFORCE);
			}
			else
				ret = pump_set_state(dhwt->pump_feed, ON, NOFORCE);	// if sensor fails, turn on the pump unconditionally during heatsource charge
		}
		else {				// no charge or electric charge
			test = FORCE;	// by default, force pump_feed immediate turn off

			// if available, test for inlet water temp
			ret = hardware_sensor_clone_temp(dhwt->set.tid_win, &water_temp);
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is > dhwt current temp, we can apply cooldown
				if (water_temp > curr_temp)
					test = NOFORCE;
			}

			// turn off pump with conditional cooldown
			ret = pump_set_state(dhwt->pump_feed, OFF, test);
		}

		if (ALL_OK != ret)
			dbgerr("\"%s\": failed to set pump_feed \"%s\" state (%d)", dhwt->name, dhwt->pump_feed->name, ret);
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
