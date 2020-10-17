//
//  plant/boiler.c
//  rwchcd
//
//  (C) 2017-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler operation implementation.
 *
 * The boiler implementation supports:
 * - Single-stage constant output burner
 * - Automatic frost protection in all operation modes
 * - Burner minimum continuous on/off time to reduce wear
 * - Adaptative trip/untrip hysteresis with low and high temperature limits
 * - Automatic boiler "sleeping" turn-off based on last heat request time
 * - Several automatic turn-off strategies
 * - Boiler minimum and maximum temperature (with signalling to consumers)
 * - Return water minimum temperature (with or without return mixing valve)
 * - Consummer delay after burner run (to prevent overheating)
 * - Burner turn-on anticipation
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "pump.h"
#include "valve.h"
#include "lib.h"
#include "boiler.h"
#include "alarms.h"
#include "io/inputs.h"
#include "io/outputs.h"

/**
 * Checklist for safe operation of a boiler.
 * This function asserts that the boiler's mandatory sensor is working, and
 * will register an alarm and report the error if it isn't.
 * @param boiler target boiler
 * @return exec status
 */
static int boiler_runchecklist(const struct s_boiler_priv * const boiler)
{
	int ret;

	assert(boiler);

	// check that mandatory sensors are working
	ret = inputs_temperature_get(boiler->set.tid_boiler, NULL);
	if (ALL_OK != ret)
		alarms_raise(ret, _("Boiler sensor failure"), _("Boiler sens fail"));

	return (ret);
}

/**
 * Create a new boiler.
 * @return pointer to the created boiler
 * @note Will set some sane defaults for:
 * - hysteresis: 6K
 * - limit_tmin: 10C
 * - limit_tmax: 90C
 * - limit_thardmax: 100C
 * - t_freeze: 5C
 * - burner_min_time: 4mn
 */
static struct s_boiler_priv * boiler_new(void)
{
	struct s_boiler_priv * const boiler = calloc(1, sizeof(*boiler));

	// set some sane defaults
	if (boiler) {
		boiler->set.hysteresis = deltaK_to_temp(6);
		boiler->set.limit_tmin = celsius_to_temp(10);
		boiler->set.limit_tmax = celsius_to_temp(90);
		boiler->set.limit_thardmax = celsius_to_temp(100);
		boiler->set.t_freeze = celsius_to_temp(5);
		boiler->set.burner_min_time = 60 * 4;	// 4mn
	}

	return (boiler);
}

/**
 * Delete a boiler.
 * Frees all boiler-local resources
 * @param priv the boiler to delete
 */
static void boiler_hscb_del_priv(void * priv)
{
	struct s_boiler_priv * boiler = priv;

	if (!boiler)
		return;

	free(boiler);
}

/**
 * Return current boiler temperature.
 * @param heat heatsource parent structure
 * @return current temperature
 * @warning no parameter check
 */
static temp_t boiler_hscb_temp(struct s_heatsource * const heat)
{
	const struct s_boiler_priv * const boiler = heat->priv;
	temp_t temp;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	inputs_temperature_get(boiler->set.tid_boiler, &temp);

	return (temp);
}

/**
 * Return last time boiler temperature was updated.
 * @param heat heatsource parent structure
 * @return last update time
 * @warning no parameter check
 */
static timekeep_t boiler_hscb_time(struct s_heatsource * const heat)
{
	const struct s_boiler_priv * const boiler = heat->priv;
	timekeep_t ttime;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	inputs_temperature_time(boiler->set.tid_boiler, &ttime);

	return (ttime);
}

/**
 * Put boiler online.
 * Perform all necessary actions to prepare the boiler for service.
 * @param heat heatsource parent structure
 * @return exec status
 * @warning no parameter check
 */
static int boiler_hscb_online(struct s_heatsource * const heat)
{
	const struct s_boiler_priv * const boiler = heat->priv;
	int ret;

	if ((HS_BOILER != heat->set.type) || !boiler)
		return (-EINVALID);

	// check that mandatory sensors are set
	ret = inputs_temperature_get(boiler->set.tid_boiler, NULL);
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!boiler->set.limit_tmax) {
		pr_err(_("\"%s\": limit_tmax must be set"), heat->name);
		ret = -EMISCONFIGURED;
	}

	// check that hardmax is > tmax (effectively checks that it's set too)
	if (boiler->set.limit_thardmax < boiler->set.limit_tmax) {
		pr_err(_("\"%s\": limit_thardmax must be set and > limit_tmax"), heat->name);
		ret = -EMISCONFIGURED;
	}

	// check that tmax > tmin
	if (boiler->set.limit_tmax < boiler->set.limit_tmin) {
		pr_err(_("\"%s\": limit_tmax must be > limit_tmin"), heat->name);
		ret = -EMISCONFIGURED;
	}

	// if pump exists check it's correctly configured
	if (boiler->set.p.pump_load && !boiler->set.p.pump_load->set.configured) {
		pr_err(_("\"%s\": pump_load \"%s\" is set but not configured"), heat->name, boiler->set.p.pump_load->name);
		ret = -EMISCONFIGURED;
	}

	if (boiler->set.limit_treturnmin) {
		// if return min is set make sure the associated sensor is configured.
		ret = inputs_temperature_get(boiler->set.tid_boiler_return, NULL);
		if (ALL_OK != ret) {
			pr_err(_("\"%s\": limit_treturnmin is set but return sensor is unavaiable (%d)"), heat->name, ret);
			ret = -EMISCONFIGURED;
		}
		// treturnmin should never be higher than tmax (and possibly not higher than tmin either)
		if (boiler->set.limit_treturnmin > boiler->set.limit_tmax) {
			pr_err(_("\"%s\": limit_treturnmin must be < limit_tmax"), heat->name);
			ret = -EMISCONFIGURED;
		}
	}

out:
	return (ret);
}

/**
 * Put boiler offline.
 * Perform all necessary actions to completely shut down the boiler.
 * @param heat heatsource parent structure
 * @return exec status
 * @warning no parameter check
 */
static int boiler_hscb_offline(struct s_heatsource * const heat)
{
	struct s_boiler_priv * const boiler = heat->priv;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	// reset runtime
	memset(&boiler->run, 0x0, sizeof(boiler->run));

	(void)!outputs_relay_state_set(boiler->set.rid_burner_1, OFF);
	(void)!outputs_relay_state_set(boiler->set.rid_burner_2, OFF);

	if (boiler->set.p.pump_load)
		pump_shutdown(boiler->set.p.pump_load);

	return (ALL_OK);
}

/**
 * Safety routine to apply to boiler in case of emergency.
 * - The burner is disabled
 * - The load pump is forced on
 * @param boiler target boiler
 */
static void boiler_failsafe(struct s_boiler_priv * const boiler)
{
	assert(boiler);

	// reset integrals
	reset_intg(&boiler->run.boil_itg);
	reset_intg(&boiler->run.ret_itg);

	(void)!outputs_relay_state_set(boiler->set.rid_burner_1, OFF);
	(void)!outputs_relay_state_set(boiler->set.rid_burner_2, OFF);
	// failsafe() is called after runchecklist(), the above can't fail

	if (boiler->set.p.pump_load)
		(void)!pump_set_state(boiler->set.p.pump_load, ON, FORCE);
}

/**
 * Boiler self-antifreeze protection.
 * This ensures that the temperature of the boiler body cannot go below a set point.
 * @param boiler target boiler
 * @return error status
 */
static void boiler_antifreeze(struct s_boiler_priv * const boiler)
{
	temp_t boilertemp;

	assert(boiler);

	(void)!inputs_temperature_get(boiler->set.tid_boiler, &boilertemp);
	// antifreeze() is called after runchecklist(), the above can't fail

	// trip at set.t_freeze point
	if (boilertemp <= boiler->set.t_freeze)
		boiler->run.antifreeze = true;

	// untrip when boiler reaches set.limit_tmin + hysteresis/2
	if (boiler->run.antifreeze) {
		if (boilertemp > (boiler->set.limit_tmin + boiler->set.hysteresis/2))
			boiler->run.antifreeze = false;
	}
}

/**
 * Boiler logic.
 * As a special case in the plant, antifreeze takes over all states if the boiler is configured (and online).
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler)
 * @note cold startup protection has a hardcoded 2% per 1Ks ratio
 */
static int boiler_hscb_logic(struct s_heatsource * restrict const heat)
{
	const timekeep_t deriv_tau = timekeep_sec_to_tk(120);
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t temp_intgrl, ret_temp = 0, target_temp = RWCHCD_TEMP_NOREQUEST;
	int_fast16_t cshift_boil = 0, cshift_ret = 0;
	timekeep_t boiler_ttime, ttime;
	int ret;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	// safe operation check
	ret = boiler_runchecklist(boiler);
	if (unlikely(ALL_OK != ret)) {
		boiler_failsafe(boiler);
		return (ret);
	}

	// Check if we need antifreeze
	boiler_antifreeze(boiler);

	switch (heat->run.runmode) {
		case RM_OFF:
			break;
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = heat->run.temp_request;
			break;
		case RM_TEST:
			target_temp = boiler->set.limit_tmax;	// set max temp to (safely) trigger burner operation
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// bypass target_temp if antifreeze is active
	if (unlikely(boiler->run.antifreeze))
		target_temp = (target_temp < boiler->set.limit_tmin) ? boiler->set.limit_tmin : target_temp;	// max of the two

	// enforce limits
	if (RWCHCD_TEMP_NOREQUEST != target_temp) {	// only if we have an actual heat request
		if (target_temp < boiler->set.limit_tmin)
			target_temp = boiler->set.limit_tmin;
		else if (target_temp > boiler->set.limit_tmax)
			target_temp = boiler->set.limit_tmax;
	}
	else {	// we don't have a temp request
		// if IDLE_NEVER, boiler always runs at min temp
		if (IDLE_NEVER == boiler->set.idle_mode)
			target_temp = boiler->set.limit_tmin;
		// if IDLE_FROSTONLY, boiler runs at min temp unless RM_FROSTFREE
		else if ((IDLE_FROSTONLY == boiler->set.idle_mode) && (RM_FROSTFREE != heat->run.runmode))
			target_temp = boiler->set.limit_tmin;
		// in all other cases the boiler will not be issued a heat request and will be stopped if run.could_sleep is set
		else if (!heat->run.could_sleep)
			target_temp = boiler->set.limit_tmin;
		else
			heat->run.runmode = RM_OFF;
	}

	boiler->run.target_temp = target_temp;

	ret = inputs_temperature_get(boiler->set.tid_boiler, &boiler->run.actual_temp);
	inputs_temperature_time(boiler->set.tid_boiler, &boiler_ttime);

	// ensure boiler is within safety limits
	if (unlikely((ALL_OK != ret) || (boiler->run.actual_temp > boiler->set.limit_thardmax))) {
		boiler_failsafe(boiler);
		heat->run.cshift_crit = RWCHCD_CSHIFT_MAX;
		heat->run.overtemp = true;
		return (-ESAFETY);
	}

	/* Always compute boiler temp derivative over the past 2mn
	 * this will make the derivative lag behind true value, but since we're only interested in the time
	 * difference between two arbitrary values computed with the same lag, it doesn't matter. */
	/// @todo variable tau
	temp_expw_deriv(&boiler->run.temp_drv, boiler->run.actual_temp, boiler_ttime, deriv_tau);

	/// @todo review integral jacketing - maybe use a PI(D) instead?
	// handle boiler minimum temp if set
	if (boiler->set.limit_tmin) {
		// calculate boiler integral
		// jacket integral between 0 and -100Ks - XXX hardcoded
		temp_intgrl = temp_thrs_intg(&boiler->run.boil_itg, boiler->set.limit_tmin, boiler->run.actual_temp, boiler_ttime, (signed)timekeep_sec_to_tk(deltaK_to_temp(-100)), 0);
		// percentage of shift is formed by the integral of current temp vs expected temp: 1Ks is -2% shift - XXX hardcoded
		cshift_boil = timekeep_tk_to_sec(temp_to_ikelvind(2 * temp_intgrl));

		dbgmsg(2, (temp_intgrl < 0), "\"%s\": boil integral: %d mKs, cshift: %d%%", heat->name, temp_intgrl, cshift_boil);
	}

	// handler boiler return temp if set - @todo Consider handling of pump_load. Consider adjusting target temp
	if (boiler->set.limit_treturnmin) {
		// if we have a configured valve, use it
		if (boiler->set.p.valve_ret) {
			// set valve for target limit. If return is higher valve will be full closed.
			ret = valve_mix_tcontrol(boiler->set.p.valve_ret, boiler->set.limit_treturnmin);
			if (unlikely((ALL_OK != ret) && (-EDEADZONE != ret)))	// something bad happened. XXX further action?
				dbgerr("\"%s\": failed to control return valve \"%s\" (%d)", heat->name, boiler->set.p.valve_ret->name, ret);
		}
		else {
			// calculate return integral
			ret = inputs_temperature_get(boiler->set.tid_boiler_return, &ret_temp);
			(void)!inputs_temperature_time(boiler->set.tid_boiler_return, &ttime);
			if (likely(ALL_OK == ret)) {
				// jacket integral between 0 and -1000Ks - XXX hardcoded
				temp_intgrl = temp_thrs_intg(&boiler->run.ret_itg, boiler->set.limit_treturnmin, ret_temp, ttime, (signed)timekeep_sec_to_tk(deltaK_to_temp(-1000)), 0);
				// percentage of shift is formed by the integral of current temp vs expected temp: 10Ks is -1% shift - XXX hardcoded
				cshift_ret = timekeep_tk_to_sec(temp_to_ikelvind(temp_intgrl / 10));

				dbgmsg(2, (temp_intgrl < 0), "\"%s\": ret integral: %d mKs, cshift: %d%%", heat->name, temp_intgrl, cshift_ret);
			}
			else
				reset_intg(&boiler->run.ret_itg);
		}
	}

	// min each cshift (they're negative) to form the heatsource critical shift
	heat->run.cshift_crit = (cshift_boil < cshift_ret) ? cshift_boil : cshift_ret;
	dbgmsg(1, (heat->run.cshift_crit), "\"%s\": cshift_crit: %d%%", heat->name, heat->run.cshift_crit);

	return (ALL_OK);
}

/**
 * Implement basic single stage boiler.
 * The boiler default trip/untrip points are target +/- hysteresis/2, with the following adaptiveness:
 * - On the low end of the curve (low temperatures):
 *   - trip temp cannot be lower than limit_tmin;
 *   - untrip temp is proportionately adjusted (increased) to allow for the full hysteresis swing;
 *   - if heat request is < trip temp, the above full hysteresis swing will be proportionately reduced,
 *     down to a minimum of hysteresis/2 (e.g. if hysteresis is 8C, and request is 2C below trip temp,
 *     hysteresis will be reduced to 6C swinging above trip temp).
 * - On the high end of the curve (high temperatures):
 *   - untrip temp cannot be higher than limit_tmax.
 *
 * @note As a special case in the plant, antifreeze takes over all states if the boiler is configured (and online).
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler)
 * @warning no parameter check
 * @todo XXX TODO: implement 2nd stage
 */
static int boiler_hscb_run(struct s_heatsource * const heat)
{
	const uint32_t fpdec = 0x8000;	// good for up to 3,5h burner run time if TIMEKEEP_SMULT==10
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t trip_temp, untrip_temp, temp_deriv, temp, ret_temp;
	timekeep_t elapsed, now;
	int ret;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	switch (heat->run.runmode) {
		case RM_OFF:
			if (!boiler->run.antifreeze)
				return (boiler_hscb_offline(heat));	// Only if no antifreeze (see above)
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
		case RM_TEST:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the boiler is active (online or antifreeze)

	// Ensure safety first
#if 0	// already executed in _logic()
	// check we can run
	ret = boiler_runchecklist(boiler);
	if (unlikely(ALL_OK != ret)) {
		boiler_failsafe(boiler);
		return (ret);
	}

	ret = inputs_temperature_get(boiler->set.tid_boiler, &boiler_temp);
	inputs_temperature_time(boiler->set.tid_boiler, &boiler_ttime);

	// ensure boiler is within safety limits
	if (unlikely((ALL_OK != ret) || (boiler_temp > boiler->set.limit_thardmax))) {
		boiler_failsafe(boiler);
		heat->run.cshift_crit = RWCHCD_CSHIFT_MAX;
		heat->run.overtemp = true;
		return (-ESAFETY);
	}
#endif

	// we're good to go

	temp_deriv = temp_expw_deriv_val(&boiler->run.temp_drv);

	// overtemp turn off at 2K hardcoded histeresis
	if (unlikely(heat->run.overtemp) && (boiler->run.actual_temp < (boiler->set.limit_thardmax - deltaK_to_temp(2))))
		heat->run.overtemp = false;

	// turn pump on if any
	if (boiler->set.p.pump_load) {
		ret = pump_set_state(boiler->set.p.pump_load, ON, 0);
		if (unlikely(ALL_OK != ret)) {
			dbgerr("\"%s\": failed to set pump_load \"%s\" ON (%d)", heat->name, boiler->set.p.pump_load->name, ret);
			boiler_failsafe(boiler);
			return (ret);	// critical error: stop there
		}
	}

	/* un/trip points */
	// apply trip_temp only if we have a heat request
	if (likely(RWCHCD_TEMP_NOREQUEST != boiler->run.target_temp)) {
		trip_temp = (boiler->run.target_temp - boiler->set.hysteresis/2);

		if (trip_temp < boiler->set.limit_tmin)
			trip_temp = boiler->set.limit_tmin;

		// compute anticipation-corrected trip_temp - only on decreasing temperature
		if (temp_deriv < 0) {
			uint64_t temp64 = (unsigned)-temp_deriv;
			temp64 *= (unsigned)-temp_deriv;
			// The above two lines are the only construct that yields the expected assembly result (rsb/umull)
			temp64 /= LIB_DERIV_FPDEC;
			temp64 *= boiler->run.turnon_curr_adj;
			temp64 /= fpdec;
			if (temp64 > INT32_MAX)
				dbgerr("temp64 result overflow: %lld, deriv: %d, curradj: %d", temp64, temp_deriv, boiler->run.turnon_curr_adj);
			else {
				temp = (temp_t)temp64;
				dbgmsg(2, 1, "\%s\": orig trip_temp: %.1f, adj: %.1f, new: %.1f", heat->name, temp_to_celsius(trip_temp), temp_to_deltaK(temp), temp_to_celsius(trip_temp + temp));
				if (temp > boiler->set.hysteresis/2)
					dbgerr("adj overflow: %.1f, curr temp: %.1f, deriv: %d, curradj: %d", temp_to_deltaK(temp), temp_to_celsius(boiler->run.actual_temp), temp_deriv, boiler->run.turnon_curr_adj);
				trip_temp += (temp > boiler->set.hysteresis/2) ? boiler->set.hysteresis/2 : temp;	// cap adjustment at hyst/2 to work around overflow
			}
		}

		// cap trip_temp at limit_tmax - hysteresis/2
		temp = (boiler->set.limit_tmax - boiler->set.hysteresis/2);
		if (trip_temp > temp)
			trip_temp = temp;
	}
	else
		trip_temp = 0;

	// always apply untrip temp (stop condition must always exist): untrip = target + hyst/2
	untrip_temp = (boiler->run.target_temp + boiler->set.hysteresis/2);

	// operate at constant hysteresis on the low end: when trip_temp is flored, shift untrip; i.e. when limit_tmin < target < (limit_tmin + hyst/2), trip == tmin and untrip == tmin + hyst
	untrip_temp += (boiler->set.hysteresis - (untrip_temp - trip_temp));

	// allow shifting down untrip temp if actual heat request goes below trip_temp (e.g. when trip_temp = limit_tmin)...
	temp = trip_temp - heat->run.temp_request;
	untrip_temp -= (temp > 0) ? temp : 0;

	// in any case untrip_temp should always be at least trip_temp + hysteresis/2. (if untrip < (trip + hyst/2) => untrip = trip + hyst/2)
	temp = (boiler->set.hysteresis/2) - (untrip_temp - trip_temp);
	untrip_temp += (temp > 0) ? temp : 0;

	// cap untrip temp at limit_tmax
	if (untrip_temp > boiler->set.limit_tmax)
		untrip_temp = boiler->set.limit_tmax;

	// return value within hysteresis
	ret = ALL_OK;

	/* burner control */
	now = timekeep_now();
	// cooldown is applied to both turn-on and turn-off to avoid pumping effect that could damage the burner - state_get() is assumed not to fail
	if ((boiler->run.actual_temp < trip_temp) && !outputs_relay_state_get(boiler->set.rid_burner_1)) {		// trip condition
		elapsed = now - boiler->run.burner_1_last_switch;
		if (elapsed >= boiler->set.burner_min_time) {	// cooldown start
			ret = outputs_relay_state_set(boiler->set.rid_burner_1, ON);
			boiler->run.burner_1_last_switch = now;
		}
	}
	else if ((boiler->run.actual_temp > untrip_temp) && outputs_relay_state_get(boiler->set.rid_burner_1)) {	// untrip condition
		elapsed = now - boiler->run.burner_1_last_switch;
		if (elapsed >= boiler->set.burner_min_time) {	// delayed stop
			ret = outputs_relay_state_set(boiler->set.rid_burner_1, OFF);
			boiler->run.burner_1_last_switch = now;
		}
	}

	// computations performed while burner is on
	if (outputs_relay_state_get(boiler->set.rid_burner_1) > 0) {
		// if boiler temp is > limit_tmin, as long as the burner is running we reset the cooldown delay
		if (boiler->set.limit_tmin < boiler->run.actual_temp)
			heat->run.target_consumer_sdelay = heat->set.consumer_sdelay;

		// compute turn-on anticipation for next run
		if (temp_deriv < 0) {
			if (!boiler->run.negderiv_starttime) {
				boiler->run.turnon_negderiv = temp_deriv;
				boiler->run.negderiv_starttime = timekeep_now();
			}
		}
		else {
			// once the derivative goes positive we now we can turn off the current offset (which will reset the untrip shift) and store the next value
			if (!boiler->run.turnon_next_adj && boiler->run.negderiv_starttime) {
				// compute an adjustement compound value that reflects the relative power drain at computation time (via turnon_negderiv)
				// the resulting value is a positive number congruent to time / temp_deriv. This value should not be averaged as the denominator can change.
				/* NB: in the case of a 2-stage or variable output burner, this computation result would be physically linked to the power output of the burner itself.
				   in the context of a single-stage constant output burner the approximation works but if we wanted to be more refined we would have to factor that output
				   level in the stored data. XXX TODO */
				boiler->run.turnon_next_adj = (timekeep_now() - boiler->run.negderiv_starttime) * fpdec;
				boiler->run.turnon_next_adj /= (unsigned)-boiler->run.turnon_negderiv;
				boiler->run.turnon_curr_adj = 0;	// reset current value
			}
		}
	}
	else {
		// boiler has turned off, store next offset in current value and reset for next run
		if (!boiler->run.turnon_curr_adj) {
			boiler->run.turnon_curr_adj = boiler->run.turnon_next_adj;
			boiler->run.turnon_next_adj = 0;		// reset next value
			boiler->run.negderiv_starttime = 0;		// reset time start
		}
	}

#ifdef DEBUG
	(void)!inputs_temperature_get(boiler->set.tid_boiler_return, &ret_temp);
	dbgmsg(1, 1, "\"%s\": on: %d, hrq_t: %.1f, tg_t: %.1f, cr_t: %.1f, trip_t: %.1f, untrip_t: %.1f, ret: %.1f, deriv: %d, curradj: %d",
	       heat->name, outputs_relay_state_get(boiler->set.rid_burner_1), temp_to_celsius(heat->run.temp_request), temp_to_celsius(boiler->run.target_temp),
	       temp_to_celsius(boiler->run.actual_temp), temp_to_celsius(trip_temp), temp_to_celsius(untrip_temp), temp_to_celsius(ret_temp), temp_deriv, boiler->run.turnon_curr_adj);
#endif
	return (ret);
}

/**
 * Boiler heatsource.
 * Sets up the target heatsource to operate as a boiler heatsource.
 * @param heat heatsource parent structure
 * @return exec status. If error the heatsource will not be operable.
 */
int boiler_heatsource(struct s_heatsource * const heat)
{
	if (!heat)
		return (-EINVALID);

	if ((HS_NONE != heat->set.type) || (heat->priv))
		return (-EEXISTS);

	heat->priv = boiler_new();
	if (!heat->priv)
		return (-EOOM);

	heat->cb.online = boiler_hscb_online;
	heat->cb.offline = boiler_hscb_offline;
	heat->cb.logic = boiler_hscb_logic;
	heat->cb.run = boiler_hscb_run;
	heat->cb.temp = boiler_hscb_temp;
	heat->cb.time = boiler_hscb_time;
	heat->cb.del_priv = boiler_hscb_del_priv;

	heat->set.type = HS_BOILER;

	return (ALL_OK);
}

