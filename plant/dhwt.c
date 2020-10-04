//
//  plant/dhwt.c
//  rwchcd
//
//  (C) 2017-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT operation implementation.
 *
 * The DHWT implementation supports:
 * - boiler-integrated tanks (by setting temp_inoffset to a near-zero value, assuming the boiler temp equals the DHWT temp; and making sure the chosen target temp and hysteresis align with the settings of the heatsource).
 * - automatic switch-over to (optional) integrated electric-heating.
 * - single and dual sensor operation (top/bottom) with adaptive hysteresis strategies.
 * - timed feedpump cooldown at untrip with temperature discharge protection.
 * - 5 charge priority models (no priority, parallel or absolute; with heat request selection).
 * - forced manual charge.
 * - 3 RM_COMFORT mode charge forcing models (never force charge, force first charge of the day, force all comfort charges).
 * - charge duration cap.
 * - DHW circulator pump.
 * - min/max limits on DHW temperature.
 * - maximum intake temperature limit.
 * - periodic anti-legionella high heat charge.
 * - isolation valve.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>
#include <time.h>	// localtime() used by dhwt_logic()

#include "pump.h"
#include "valve.h"
#include "dhwt.h"
#include "lib.h"
#include "runtime.h"
#include "scheduler.h"
#include "io/inputs.h"
#include "io/outputs.h"

/**
 * Create a dhwt
 * @return the newly created dhwt or NULL
 */
struct s_dhwt * dhwt_new(void)
{
	struct s_dhwt * const dhwt = calloc(1, sizeof(*dhwt));
	return (dhwt);
}

/**
 * Put dhwt online.
 * Perform all necessary actions to prepare the dhwt for service and
 * mark it as online if all checks pass.
 * @param dhwt target dhwt
 * @return exec status
 */
int dhwt_online(struct s_dhwt * const dhwt)
{
	temp_t temp;
	int ret = -EGENERIC;

	assert(dhwt->pdata);

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are set
	ret = inputs_temperature_get(dhwt->set.tid_bottom, NULL);
	if (ALL_OK != ret)
		ret = inputs_temperature_get(dhwt->set.tid_top, NULL);
	if (ret)
		goto out;

	// limit_tmin must be > 0C
	temp = SETorDEF(dhwt->set.params.limit_tmin, dhwt->pdata->set.def_dhwt.limit_tmin);
	if (temp <= celsius_to_temp(0)) {
		pr_err(_("\"%s\": limit_tmin must be locally or globally > 0°C"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	// limit_tmax must be > limit_tmin
	if (SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax) <= temp) {
		pr_err(_("\"%s\": limit_tmax must be locally or globally > limit_tmin"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	if (dhwt->set.anti_legionella) {
		if (SETorDEF(dhwt->set.params.t_legionella, dhwt->pdata->set.def_dhwt.t_legionella) <= 0) {
			pr_err(_("\"%s\": anti_legionella is set: t_legionella must be locally or globally > 0°K!"), dhwt->name);
			ret = -EMISCONFIGURED;
		}
	}

	// hysteresis must be > 0K
	if (SETorDEF(dhwt->set.params.hysteresis, dhwt->pdata->set.def_dhwt.hysteresis) <= 0) {
		pr_err(_("\"%s\": hysteresis must be locally or globally > 0°K!"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	// t_frostfree must be > 0C
	temp = SETorDEF(dhwt->set.params.t_frostfree, dhwt->pdata->set.def_dhwt.t_frostfree);
	if (temp <= celsius_to_temp(0)) {
		pr_err(_("\"%s\": t_frostfree must be locally or globally > 0°C!"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	// t_comfort must be > t_frostfree
	if (SETorDEF(dhwt->set.params.t_comfort, dhwt->pdata->set.def_dhwt.t_comfort) < temp) {
		pr_err(_("\"%s\": t_comfort must be locally or globally > t_frostfree"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	// t_eco must be > t_frostfree
	if (SETorDEF(dhwt->set.params.t_eco, dhwt->pdata->set.def_dhwt.t_eco) < temp) {
		pr_err(_("\"%s\": t_eco must be locally or globally > t_frostfree"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

	// if pumps exist check they're correctly configured
	if (dhwt->set.p.pump_feed && !dhwt->set.p.pump_feed->set.configured) {
		pr_err(_("\"%s\": pump_feed \"%s\" is set but not configured"), dhwt->name, dhwt->set.p.pump_feed->name);
		ret = -EMISCONFIGURED;
	}

	if (dhwt->set.p.pump_recycle && !dhwt->set.p.pump_recycle->set.configured) {
		pr_err(_("\"%s\": pump_recycle \"%s\" is set but not configured"), dhwt->name, dhwt->set.p.pump_recycle->name);
		ret = -EMISCONFIGURED;
	}

	if (dhwt->set.p.valve_hwisol && !dhwt->set.p.valve_hwisol->set.configured) {
		pr_err(_("\"%s\": valve_hwisol \"%s\" is set but not configured"), dhwt->name, dhwt->set.p.valve_hwisol->name);
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
 *
 * @note the pump_feed is @b NOT unflagged when running electric to avoid sending
 * cold water into the feed circuit. Thus the pump_feed cannot be "summer maintained"
 * when the DHWT is running electric.
 * @param dhwt target dhwt
 * @param active flag status
 */
static inline void dhwt_actuator_use(struct s_dhwt * const dhwt, bool active)
{
	assert(dhwt);

	if (dhwt->set.p.pump_feed)
		dhwt->set.p.pump_feed->run.dwht_use = active;

	if (dhwt->set.p.pump_recycle)
		dhwt->set.p.pump_recycle->run.dwht_use = active;
}

/**
 * Put dhwt offline.
 * Perform all necessary actions to completely shut down the dhwt.
 * @param dhwt target dhwt
 * @return error status
 */
int dhwt_shutdown(struct s_dhwt * const dhwt)
{
	assert(dhwt);
	assert(dhwt->set.configured);

	// XXX ensure pumps are stopped after summer maintenance
	if (dhwt->set.p.pump_feed)
		pump_shutdown(dhwt->set.p.pump_feed);

	if (dhwt->set.p.pump_recycle)
		pump_shutdown(dhwt->set.p.pump_recycle);

	if (!dhwt->run.active)
		return (ALL_OK);

	// clear runtime data while preserving online state
	dhwt->run.charge_on = false;
	dhwt->run.recycle_on = false;
	dhwt->run.force_on = false;
	dhwt->run.legionella_on = false;
	dhwt->run.charge_overtime = false;
	dhwt->run.electric_mode = false;
	dhwt->run.mode_since = 0;	// XXX
	dhwt->run.charge_yday = 0;	// XXX

	dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	dhwt->run.target_temp = 0;

	dhwt_actuator_use(dhwt, false);

	(void)!outputs_relay_state_set(dhwt->set.rid_selfheater, OFF);

	// isolate DHWT if possible
	if (dhwt->set.p.valve_hwisol)
		(void)!valve_isol_trigger(dhwt->set.p.valve_hwisol, true);

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
int dhwt_offline(struct s_dhwt * const dhwt)
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
 * DHWT logic.
 * Sets DHWT target temperature based on selected run mode.
 * Enforces programmatic use of force charge when necessary.
 * @param dhwt target dhwt
 * @return exec status
 */
__attribute__((warn_unused_result))
static int dhwt_logic(struct s_dhwt * restrict const dhwt)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	const struct s_schedule_eparams * eparams;
	const time_t tnow = time(NULL);
	const struct tm * const ltime = localtime(&tnow);	// localtime handles DST and TZ for us
	enum e_runmode prev_runmode;
	temp_t target_temp, ltmin, ltmax;

	assert(runtime);

	assert(dhwt);

	// store current status for transition detection
	prev_runmode = dhwt->run.runmode;

	// handle global/local runmodes
	if (RM_AUTO == dhwt->set.runmode) {
		// if we have a schedule, use it, or global settings if unavailable
		eparams = scheduler_get_schedparams(dhwt->set.schedid);
		if ((SYS_AUTO == runtime->run.systemmode) && eparams) {
			dhwt->run.runmode = eparams->dhwmode;
			dhwt->run.legionella_on = eparams->legionella;
			dhwt->run.recycle_on = (dhwt->run.electric_mode) ? (eparams->recycle && dhwt->set.electric_recycle) : eparams->recycle;
		}
		else	// don't touch legionella/recycle
			dhwt->run.runmode = runtime->run.dhwmode;
	}
	else
		dhwt->run.runmode = dhwt->set.runmode;

	// force DHWT ON during hs_overtemp condition
	if (unlikely(dhwt->pdata->run.hs_overtemp))
		dhwt->run.runmode = RM_COMFORT;

	// depending on dhwt run mode, assess dhwt target temp
	switch (dhwt->run.runmode) {
		case RM_OFF:
		case RM_TEST:
			return (ALL_OK);	// No further processing
		case RM_ECO:
			if (!dhwt->run.electric_mode) {
				target_temp = SETorDEF(dhwt->set.params.t_eco, dhwt->pdata->set.def_dhwt.t_eco);
				break;
			}
			// fallthrough - we don't support eco on electric due to expected inertia
		case RM_COMFORT:
			target_temp = SETorDEF(dhwt->set.params.t_comfort, dhwt->pdata->set.def_dhwt.t_comfort);
			break;
		case RM_FROSTFREE:
			target_temp = SETorDEF(dhwt->set.params.t_frostfree, dhwt->pdata->set.def_dhwt.t_frostfree);
			break;
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if anti-legionella charge is requested, enforce temp and bypass logic
	if (unlikely(dhwt->run.legionella_on)) {
		target_temp = SETorDEF(dhwt->set.params.t_legionella, dhwt->pdata->set.def_dhwt.t_legionella);
		dhwt->run.force_on = true;
		dhwt->run.recycle_on = dhwt->set.legionella_recycle;
		goto settarget;
	}

	// transition detection
	if (unlikely(prev_runmode != dhwt->run.runmode)) {
		// handle programmed forced charges at COMFORT switch on
		if (RM_COMFORT == dhwt->run.runmode) {
			if (DHWTF_ALWAYS == dhwt->set.force_mode)
				dhwt->run.force_on = true;
			else if ((DHWTF_FIRST == dhwt->set.force_mode) && (ltime->tm_yday != dhwt->run.charge_yday)) {
				dhwt->run.force_on = true;
				dhwt->run.charge_yday = ltime->tm_yday;
			}
		}
	}

	// enforce limits on dhw temp
	ltmin = SETorDEF(dhwt->set.params.limit_tmin, dhwt->pdata->set.def_dhwt.limit_tmin);
	ltmax = SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax);
	if (target_temp < ltmin)
		target_temp = ltmin;
	else if (target_temp > ltmax)
		target_temp = ltmax;

	// force maximum temp during hs_overtemp condition
	if (unlikely(dhwt->pdata->run.hs_overtemp)) {
		target_temp = ltmax;
		dhwt->run.force_on = true;
	}

settarget:
	// save current target dhw temp
	dhwt->run.target_temp = target_temp;

	return (ALL_OK);
}

/**
 * DHWT failsafe routine.
 * By default we shutdown the tank. If configured for
 * electric failover the self-heater is still turned on unconditionnally
 * (this assumes that the self-heater has a local thermostat, which should always be the case).
 * The major inconvenient here is that this failsafe mode COULD provoke a DHWT
 * freeze in the most adverse conditions.
 * @warning DHWT could freeze - TODO: needs review
 * @param dhwt target dhwt
 */
static void dhwt_failsafe(struct s_dhwt * restrict const dhwt)
{
	int ret;

	assert(dhwt);

	dbgerr("\"%s\": failsafe mode!", dhwt->name);

	dhwt_shutdown(dhwt);

	ret = outputs_relay_state_set(dhwt->set.rid_selfheater, dhwt->set.electric_failover ? ON : OFF);
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
 * @note discharge protection will fail if the input sensor needs water flow
 * in the pump_feed. It is thus important to ensure that the water input temperature sensor
 * can provide a reliable reading even when the feedpump is off.
 * @note there is a short window during which the feed pump could be operating while
 * the isolation valve is still partially closed (if a charge begins immediately at first turn-on).
 * @note An ongoing anti-legionella charge will not be interrupted by a plant-wide change in priority.
 * @note Since anti-legionella can only be unset _after_ a complete charge (or a DHWT shutdown),
 * once the anti-legionella charge has been requested, it is @b guaranteed to happen,
 * although not necessarily at the planned time if there is delay in servicing the target DHWT priority.
 */
int dhwt_run(struct s_dhwt * const dhwt)
{
	temp_t water_temp, top_temp, bottom_temp, curr_temp, wintmax, trip_temp;
	bool valid_ttop = false, valid_tbottom = false, test;
	const timekeep_t now = timekeep_now();
	timekeep_t limit;
	int ret;

	assert(dhwt->pdata);

	if (unlikely(!dhwt))
		return (-EINVALID);

	if (unlikely(!dhwt->run.online))	// implies set.configured == true
		return (-EOFFLINE);

	ret = dhwt_logic(dhwt);
	if (unlikely(ALL_OK != ret))
		return (ret);

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
			if (dhwt->set.p.valve_hwisol)
				(void)!valve_isol_trigger(dhwt->set.p.valve_hwisol, false);
			if (dhwt->set.p.pump_feed)
				(void)!pump_set_state(dhwt->set.p.pump_feed, ON, FORCE);
			if (dhwt->set.p.pump_recycle)
				(void)!pump_set_state(dhwt->set.p.pump_recycle, ON, FORCE);
			(void)!outputs_relay_state_set(dhwt->set.rid_selfheater, ON);
			return (ALL_OK);
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active
	dhwt->run.active = true;

	// de-isolate the DHWT if necessary (when not on electric)
	if (dhwt->set.p.valve_hwisol && !dhwt->run.electric_mode) {
		ret = valve_isol_trigger(dhwt->set.p.valve_hwisol, false);
		if (ALL_OK != ret)
			dbgerr("\%s\": cannot operate isolation valve!", dhwt->name);
	}

	// check which sensors are available
	ret = inputs_temperature_get(dhwt->set.tid_bottom, &bottom_temp);
	if (ALL_OK == ret)
		valid_tbottom = true;
	ret = inputs_temperature_get(dhwt->set.tid_top, &top_temp);
	if (ALL_OK == ret)
		valid_ttop = true;

	// no sensor available, give up
	if (unlikely(!valid_tbottom && !valid_ttop)) {
		dhwt_failsafe(dhwt);
		return (ret);	// return last error
	}

	// We're good to go

	dbgmsg(1, 1, "\"%s\": on: %d, mode_since: %ld, tg_t: %.1f, bot_t: %.1f, top_t: %.1f",
	       dhwt->name, dhwt->run.charge_on, timekeep_tk_to_sec(dhwt->run.mode_since), temp_to_celsius(dhwt->run.target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));

	// handle recycle loop
	if (dhwt->set.p.pump_recycle) {
		if (dhwt->run.recycle_on)
			ret = pump_set_state(dhwt->set.p.pump_recycle, ON, NOFORCE);
		else
			ret = pump_set_state(dhwt->set.p.pump_recycle, OFF, NOFORCE);

		if (ALL_OK != ret)	// this is a non-critical error, keep going
			dbgerr("\"%s\": failed to set pump_recycle \"%s\" state (%d)", dhwt->name, dhwt->set.p.pump_recycle->name, ret);
	}

	/* handle heat charge - NOTE we enforce sensor position, it SEEMS desirable
	 apply hysteresis on logic: trip at target - hysteresis (preferably on bottom sensor),
	 untrip at target (preferably on top sensor). */
	if (!dhwt->run.charge_on) {	// no charge in progress
					// in non-electric mode: prevent charge "pumping", enforce delay between charges
		if (!dhwt->run.electric_mode) {
			limit = SETorDEF(dhwt->set.params.limit_chargetime, dhwt->pdata->set.def_dhwt.limit_chargetime);
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
			trip_temp = dhwt->run.target_temp - SETorDEF(dhwt->set.params.hysteresis, dhwt->pdata->set.def_dhwt.hysteresis);

		// trip condition
		if (curr_temp < trip_temp) {
			if (dhwt->pdata->run.plant_could_sleep && (ALL_OK == outputs_relay_state_set(dhwt->set.rid_selfheater, ON))) {
				// the plant is sleeping and we have a configured self heater: use it
				dhwt->run.electric_mode = true;
				// isolate the DHWT if possible when operating from electric
				if (dhwt->set.p.valve_hwisol)
					(void)!valve_isol_trigger(dhwt->set.p.valve_hwisol, true);

				// mark heating in progress
				dhwt->run.charge_on = true;
				dhwt->run.mode_since = now;
			}
			else if (dhwt->pdata->run.dhwt_currprio >= dhwt->set.prio) {	// run from plant heat source if prio is allowed
				dhwt->run.electric_mode = false;
				// calculate necessary water feed temp: target tank temp + offset
				water_temp = dhwt->run.target_temp + SETorDEF(dhwt->set.params.temp_inoffset, dhwt->pdata->set.def_dhwt.temp_inoffset);

				// enforce limits
				wintmax = SETorDEF(dhwt->set.params.limit_wintmax, dhwt->pdata->set.def_dhwt.limit_wintmax);
				if (water_temp > wintmax)
					water_temp = wintmax;

				// apply heat request
				dhwt->run.heat_request = water_temp;

				// mark heating in progress
				dhwt->run.charge_on = true;
				dhwt->run.mode_since = now;
			}
		}
	}
	else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
		if (valid_ttop)	// prefer top temp if available (untrip charge when top is hot)
			curr_temp = top_temp;
		else
			curr_temp = bottom_temp;

		// untrip conditions
		test = false;

		// in non-electric mode and no anti-legionella charge (never interrupt an anti-legionella charge):
		if (!dhwt->run.electric_mode && !dhwt->run.legionella_on) {
			// if heating gone overtime, untrip
			limit = SETorDEF(dhwt->set.params.limit_chargetime, dhwt->pdata->set.def_dhwt.limit_chargetime);
			if ((limit) && ((now - dhwt->run.mode_since) > limit)) {
				test = true;
				dhwt->run.charge_overtime = true;
			}
			// if DHWT exceeds current allowed prio, untrip
			if (dhwt->pdata->run.dhwt_currprio < dhwt->set.prio)
				test = true;
		}

		// if heating in progress, untrip at target temp (if we're running electric this is the only untrip condition that applies)
		if (curr_temp >= dhwt->run.target_temp)
			test = true;

		// stop all heat input (ensures they're all off at switchover)
		if (test) {
			// stop self-heater (if any)
			(void)!outputs_relay_state_set(dhwt->set.rid_selfheater, OFF);

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
	if (dhwt->set.p.pump_feed) {
		if (dhwt->run.charge_on && !dhwt->run.electric_mode) {	// on heatsource charge
									// if available, test for inlet water temp
			ret = inputs_temperature_get(dhwt->set.tid_win, &water_temp);	// Note: this sensor must not rely on pump running for accurate read, otherwise this can be a problem
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is < dhwt current temp, stop the pump
				if (water_temp < curr_temp)
					ret = pump_set_state(dhwt->set.p.pump_feed, OFF, FORCE);
				else if (water_temp >= (curr_temp + deltaK_to_temp(1)))	// 1K hysteresis
					ret = pump_set_state(dhwt->set.p.pump_feed, ON, NOFORCE);
			}
			else
				ret = pump_set_state(dhwt->set.p.pump_feed, ON, NOFORCE);	// if sensor fails, turn on the pump unconditionally during heatsource charge
		}
		else {				// no charge or electric charge
			test = FORCE;	// by default, force pump_feed immediate turn off

			// if available, test for inlet water temp
			ret = inputs_temperature_get(dhwt->set.tid_win, &water_temp);
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is > dhwt current temp, we can apply cooldown
				if (water_temp > curr_temp)
					test = NOFORCE;
			}

			// turn off pump with conditional cooldown
			ret = pump_set_state(dhwt->set.p.pump_feed, OFF, test);
		}

		if (unlikely(ALL_OK != ret))
			dbgerr("\"%s\": failed to set pump_feed \"%s\" state (%d)", dhwt->name, dhwt->set.p.pump_feed->name, ret);
	}

	return (ret);
}

/**
 * DHWT destructor.
 * Frees all dhwt-local resources
 * @param dhwt the dhwt to delete
 */
void dhwt_del(struct s_dhwt * restrict dhwt)
{
	if (!dhwt)
		return;

	free((void *)dhwt->name);
	dhwt->name = NULL;

	free(dhwt);
}
