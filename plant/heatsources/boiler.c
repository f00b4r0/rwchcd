//
//  plant/heatsources/boiler.c
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
 * - Burner failure detection
 * - Logging of state and temperatures
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * no concurrent operation is ever expected to happen to a given boiler, with the exception of
 * logging activity for which only data races are prevented via relaxed operations.
 * It is worth noting that no data consistency is guaranteed for logging, i.e. the data points logged
 * during a particular call of boiler_hs_logdata_cb() may represent values from different time frames:
 * the overhead of ensuring consistency seems overkill for the purpose served by the log facility.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "plant/pump.h"
#include "plant/valve.h"
#include "plant/heatsource_priv.h"
#include "lib.h"
#include "boiler.h"
#include "alarms.h"
#include "io/inputs.h"
#include "io/outputs.h"
#include "log/log.h"

#define BOILER_STORAGE_PREFIX	"hs_boiler"

/**
 * Boiler data log callback.
 * @param ldata the log data to populate
 * @param object the opaque pointer to parent heatsource structure
 * @return exec status
 */
static int boiler_hs_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_heatsource * const hs = object;
	const struct s_boiler_priv * const boiler = hs->priv;
	unsigned int i = 0;

	assert(ldata);
	assert(ldata->nkeys >= 7);

	if (!boiler)
		return (-EINVALID);

	if (!aler(&hs->run.online))
		return (-EOFFLINE);

	ldata->values[i++].i = aler(&hs->run.runmode);
	ldata->values[i++].i = aler(&hs->run.could_sleep);
	ldata->values[i++].i = aler(&hs->run.overtemp);
	ldata->values[i++].i = aler(&hs->run.failed);
	ldata->values[i++].f = temp_to_celsius(aler(&hs->run.temp_request));

	ldata->values[i++].f = temp_to_celsius(aler(&boiler->run.target_temp));
	ldata->values[i++].f = temp_to_celsius(aler(&boiler->run.actual_temp));

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for a given boiler.
 * @param heat the target parent heatsource
 * @return (statically allocated) s_log_source pointer
 * @warning must not be called concurrently
 */
static const struct s_log_source * boiler_hs_lsrc(const struct s_heatsource * const heat)
{
	static const log_key_t keys[] = {
		"runmode", "could_sleep", "overtemp", "failed", "temp_request", "target_temp", "actual_temp",
	};
	static const enum e_log_metric metrics[] = {
		LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE,
	};
	const log_version_t version = 1;
	static struct s_log_source Boiler_lsrc;

	Boiler_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_1mn,
		.basename = BOILER_STORAGE_PREFIX,
		.identifier = heat->name,
		.version = version,
		.logdata_cb = boiler_hs_logdata_cb,
		.nkeys = ARRAY_SIZE(keys),
		.keys = keys,
		.metrics = metrics,
		.object = heat,
	};
	return (&Boiler_lsrc);
}

/**
 * Register a boiler heatsource for logging.
 * @param heat the target parent heatsource
 * @return exec status
 */
static int boiler_hscb_log_register(const struct s_heatsource * const heat)
{
	assert(heat);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (!heat->set.log)
		return (ALL_OK);

	return (log_register(boiler_hs_lsrc(heat)));
}

/**
 * Deregister a boiler heatsource from logging.
 * @param heat the target parent heatsource
 * @return exec status
 */
static int boiler_hscb_log_deregister(const struct s_heatsource * const heat)
{
	assert(heat);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (!heat->set.log)
		return (ALL_OK);

	return (log_deregister(boiler_hs_lsrc(heat)));
}

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
	if (ALL_OK != ret) {
		pr_err(_("\"%s\": tid_boiler failed!"), heat->name);
		ret = - EMISCONFIGURED;
	}

	// check that mandatory settings are set
	if (!boiler->set.hysteresis) {
		pr_err(_("\"%s\": hysteresis must be set and > 0°K"), heat->name);
		ret = -EMISCONFIGURED;
	}

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

	// check that tfreeze is positive
	if (boiler->set.t_freeze <= celsius_to_temp(0)) {
		pr_err(_("\"%s\": tfreeze must be set and above 0°C"), heat->name);
		ret = -EMISCONFIGURED;
	}

	// if return valve exists check it's online
	if (boiler->set.p.valve_ret) {
		if (!valve_is_online(boiler->set.p.valve_ret)) {
			pr_err(_("\"%s\": valve_ret \"%s\" is set but not online"), heat->name, valve_name(boiler->set.p.valve_ret));
			ret = -EMISCONFIGURED;
		}
		else if (VA_TYPE_MIX != valve_get_type(boiler->set.p.valve_ret)) {
			pr_err(_("\"%s\": Invalid type for valve_ret \"%s\" (mixing valve expected)"), heat->name, valve_name(boiler->set.p.valve_ret));
			ret = -EMISCONFIGURED;
		}
	}

	if (boiler->set.limit_treturnmin) {
		// if return min is set make sure the associated sensor is configured.
		if (inputs_temperature_get(boiler->set.tid_boiler_return, NULL) != ALL_OK) {
			pr_err(_("\"%s\": limit_treturnmin is set but return sensor is unavaiable"), heat->name);
			ret = -EMISCONFIGURED;
		}
		// treturnmin should never be higher than tmax (and possibly not higher than tmin either)
		if (boiler->set.limit_treturnmin > boiler->set.limit_tmax) {
			pr_err(_("\"%s\": limit_treturnmin must be < limit_tmax"), heat->name);
			ret = -EMISCONFIGURED;
		}
	}

	// grab relays
	if (outputs_relay_name(boiler->set.rid_burner_1)) {
		if (outputs_relay_grab(boiler->set.rid_burner_1) != ALL_OK) {
			pr_err(_("\"%s\": rid_burner1 is unavailable"), heat->name);
			ret = -EMISCONFIGURED;
		}
	}

	if (outputs_relay_name(boiler->set.rid_burner_2)) {
		if (outputs_relay_grab(boiler->set.rid_burner_2) != ALL_OK) {
			pr_err(_("\"%s\": rid_burner2 is unavailable"), heat->name);
			ret = -EMISCONFIGURED;
		}
	}

	return (ret);
}

/**
 * Shutdown boiler.
 * Perform all necessary actions to  shut down the boiler.
 * @param boiler target boiler
 * @return exec status
 */
static int boiler_shutdown(struct s_boiler_priv * const boiler)
{
	assert(boiler);

	// ensure pumps and valves are off after summer maintenance
	if (boiler->set.p.valve_ret)
		(void)!valve_reqclose_full(boiler->set.p.valve_ret);

	if (!boiler->run.active)
		return (ALL_OK);

	boiler->run.turnon_negderiv = 0;
	boiler->run.negderiv_starttime = 0;
	boiler->run.turnon_curr_adj = 0;
	boiler->run.turnon_next_adj = 0;

	// reset integrals
	reset_intg(&boiler->run.boil_itg);
	reset_intg(&boiler->run.ret_itg);

	(void)!outputs_relay_state_set(boiler->set.rid_burner_1, OFF);
	(void)!outputs_relay_state_set(boiler->set.rid_burner_2, OFF);

	boiler->run.active = false;

	return (ALL_OK);
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

	boiler_shutdown(boiler);

	outputs_relay_thaw(boiler->set.rid_burner_1);
	outputs_relay_thaw(boiler->set.rid_burner_2);

	// reset runtime
	memset(&boiler->run, 0x0, sizeof(boiler->run));

	return (ALL_OK);
}

/**
 * Safety routine to apply to boiler in case of emergency.
 * - The burner is disabled
 * - The load pump is forced on
 * - The return valve is open in full
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

	if (boiler->set.p.valve_ret)
		(void)!valve_reqopen_full(boiler->set.p.valve_ret);
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
	timekeep_t deriv_tau;
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t actual_temp, ret_temp = 0, target_temp = RWCHCD_TEMP_NOREQUEST;
	tempdiff_t temp_intgrl;
	int_least16_t cshift_boil = 0, cshift_ret = 0;
	timekeep_t boiler_ttime, ttime;
	int ret;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	// safe operation check
	ret = boiler_runchecklist(boiler);
	if (unlikely(ALL_OK != ret)) {
		alarms_raise(ret, _("Boiler \"%s\": failed to get temp!"), heat->name);
		ret = -ESAFETY;
		goto fail;
	}

	// Check if we need antifreeze
	boiler_antifreeze(boiler);

	switch (aler(&heat->run.runmode)) {
		case RM_OFF:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		case RM_SUMMAINT:
		default:
			dbgerr("\"%s\": invalid runmode (%d), falling back to RM_FROSTREE", heat->name, aler(&heat->run.runmode));
			aser(&heat->run.runmode, RM_FROSTFREE);
			// fallthrough
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = aler(&heat->run.temp_request);
			break;
		case RM_TEST:
			target_temp = boiler->set.limit_tmax;	// set max temp to (safely) trigger burner operation
			break;
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
		else if ((IDLE_FROSTONLY == boiler->set.idle_mode) && (RM_FROSTFREE != aler(&heat->run.runmode)))
			target_temp = boiler->set.limit_tmin;
		// in all other cases the boiler will not be issued a heat request and will be stopped if run.could_sleep is set
		else if (!aler(&heat->run.could_sleep))
			target_temp = boiler->set.limit_tmin;
		else
			aser(&heat->run.runmode, RM_OFF);
	}

	aser(&boiler->run.target_temp, target_temp);

	ret = inputs_temperature_get(boiler->set.tid_boiler, &actual_temp);	// shouldn't fail: already tested in _runchecklist()
	inputs_temperature_time(boiler->set.tid_boiler, &boiler_ttime);

	aser(&boiler->run.actual_temp, actual_temp);

	// ensure boiler is within safety limits
	if (unlikely((ALL_OK != ret) || (actual_temp > boiler->set.limit_thardmax))) {
		heat->run.cshift_crit = RWCHCD_CSHIFT_MAX;
		aser(&heat->run.overtemp, true);
		ret = -ESAFETY;
		alarms_raise(ret, _("Boiler \"%s\": overheating!"), heat->name);	// assume we get here if overheating
		goto fail;
	}

	/* Always compute boiler temp derivative over the past 2mn
	 * this will make the derivative lag behind true value, but since we're only interested in the time
	 * difference between two arbitrary values computed with the same lag, it doesn't matter. */
	/// @todo variable tau
	deriv_tau = outputs_relay_state_get(boiler->set.rid_burner_1) ? timekeep_sec_to_tk(10) : timekeep_sec_to_tk(60);
	temp_lin_deriv(&boiler->run.temp_drv, actual_temp, boiler_ttime, deriv_tau);

	if (!boiler->run.active)
		goto out;	// we're done here

	/// @todo review integral jacketing - maybe use a PI(D) instead?
	// handle boiler minimum temp if set
	if (boiler->set.limit_tmin) {
		// calculate boiler integral
		// jacket integral between 0 and -100Ks - XXX hardcoded
		temp_intgrl = temp_thrs_intg(&boiler->run.boil_itg, boiler->set.limit_tmin, actual_temp, boiler_ttime, (signed)timekeep_sec_to_tk(deltaK_to_tempdiff(-100)), 0);
		// percentage of shift is formed by the integral of current temp vs expected temp: 1Ks is -2% shift - cannot overflow due to jacket - XXX hardcoded
		cshift_boil = (typeof(cshift_boil))timekeep_tk_to_sec(temp_to_ikelvind(2 * temp_intgrl));

		dbgmsg(2, (temp_intgrl < 0), "\"%s\": boil integral: %d mKs, cshift: %d%%", heat->name, temp_intgrl, cshift_boil);
	}

	// handler boiler return temp if set - @todo Consider adjusting target temp
	if (boiler->set.limit_treturnmin) {
		// if we have a configured mixing return valve, use it
		if (boiler->set.p.valve_ret) {
			// set valve for target limit. If return is higher valve will be full closed, i.e. bypass fully closed
			ret = valve_mix_tcontrol(boiler->set.p.valve_ret, boiler->set.limit_treturnmin);
			if (unlikely((ALL_OK != ret)))	// something bad happened. XXX REVIEW further action?
				alarms_raise(ret, _("Boiler \"%s\": failed to control return valve \"%s\""), heat->name, valve_name(boiler->set.p.valve_ret));
		}
		else {
			// calculate return integral
			ret = inputs_temperature_get(boiler->set.tid_boiler_return, &ret_temp);
			(void)!inputs_temperature_time(boiler->set.tid_boiler_return, &ttime);
			if (likely(ALL_OK == ret)) {
				// jacket integral between 0 and -500Ks - XXX hardcoded
				temp_intgrl = temp_thrs_intg(&boiler->run.ret_itg, boiler->set.limit_treturnmin, ret_temp, ttime, (signed)timekeep_sec_to_tk(deltaK_to_tempdiff(-500)), 0);
				// percentage of shift is formed by the integral of current temp vs expected temp: 10Ks is -1% shift - cannot overflow due to jacket at -50% - XXX hardcoded
				cshift_ret = (typeof(cshift_ret))timekeep_tk_to_sec(temp_to_ikelvind(temp_intgrl / 10));

				dbgmsg(2, (temp_intgrl < 0), "\"%s\": ret integral: %d mKs, cshift: %d%%", heat->name, temp_intgrl, cshift_ret);
			}
			else
				reset_intg(&boiler->run.ret_itg);
		}
	}

	// min each cshift (they're negative) to form the heatsource critical shift
	heat->run.cshift_crit = (cshift_boil < cshift_ret) ? cshift_boil : cshift_ret;
	dbgmsg(1, (heat->run.cshift_crit), "\"%s\": cshift_crit: %d%%", heat->name, heat->run.cshift_crit);

out:
	return (ALL_OK);

fail:
	boiler_failsafe(boiler);
	return (ret);
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
 * @todo implement summer maintenance for mixing valve
 * @note will trigger an alarm if burner stays on for >6h without heat output
 * @note this function ensures that in the event of an error, the boiler is put in a failsafe state as defined in boiler_failsafe().
 */
static int boiler_hscb_run(struct s_heatsource * const heat)
{
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t trip_temp, untrip_temp, temp, target_temp, actual_temp;
	tempdiff_t temp_deriv;
	timekeep_t elapsed, now;
	int ret;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	switch (aler(&heat->run.runmode)) {
		case RM_OFF:
			if (!boiler->run.antifreeze)
				return (boiler_shutdown(boiler));	// Only if no antifreeze (see above)
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			break;
		case RM_TEST:
			boiler->run.burner_1_last_switch -= boiler->set.burner_min_time;	// ensure it starts immediately
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		case RM_SUMMAINT:
		default:
			ret = -EINVALIDMODE;	// this can never happen due to fallback in _logic()
			goto fail;
	}

	// if we reached this point then the boiler is active (online or antifreeze)
	boiler->run.active = true;

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

	actual_temp = aler(&boiler->run.actual_temp);
	target_temp = aler(&boiler->run.target_temp);

	temp_deriv = temp_expw_deriv_val(&boiler->run.temp_drv);

	// overtemp turn off at 2K hardcoded histeresis
	if (unlikely(aler(&heat->run.overtemp)) && (actual_temp < (boiler->set.limit_thardmax - deltaK_to_temp(2))))
		aser(&heat->run.overtemp, false);

	/* un/trip points */
	// apply trip_temp only if we have a heat request
	if (likely(RWCHCD_TEMP_NOREQUEST != target_temp)) {
		trip_temp = (target_temp - boiler->set.hysteresis/2);

		if (trip_temp < boiler->set.limit_tmin)
			trip_temp = boiler->set.limit_tmin;

		// compute anticipation-corrected trip_temp - only on decreasing temperature
		if (temp_deriv < 0) {
			// curr_adj = time necessary for deriv to cross 0 divided by deriv at burner turn on: dt / (dT/dt) == dt^2 / dT
			// adjust = temp_deriv^2 * curr_adj: (dT^2/dt^2) * (dt^2/dT) == dT
			uint64_t temp64 = (unsigned)-temp_deriv;
			temp64 *= (unsigned)-temp_deriv;
			// The above two lines are the only construct that yields the expected assembly result (rsb/umull)
			temp64 /= LIB_DERIV_FPDEC;
			temp64 *= boiler->run.turnon_curr_adj;
			temp64 /= LIB_DERIV_FPDEC;

			temp = (temp_t)temp64;
			trip_temp += (temp > boiler->set.hysteresis) ? boiler->set.hysteresis : temp;	// XXX cap adjustment at hyst
			dbgmsg(2, unlikely(temp > boiler->set.hysteresis), "adj overflow: %.1f, curr temp: %.1f, deriv: %d, curradj: %d", temp_to_deltaK(temp), temp_to_celsius(actual_temp), temp_deriv, boiler->run.turnon_curr_adj);
		}

		// cap trip_temp at limit_tmax - hysteresis/2
		temp = (boiler->set.limit_tmax - boiler->set.hysteresis/2);
		if (trip_temp > temp)
			trip_temp = temp;
	}
	else
		trip_temp = 0;

	// always apply untrip temp (stop condition must always exist):
	untrip_temp = trip_temp + boiler->set.hysteresis;

	// allow shifting down untrip temp if actual heat request goes below trip_temp (e.g. when trip_temp = limit_tmin)...
	temp = trip_temp - aler(&heat->run.temp_request);
	untrip_temp -= ((tempdiff_t)temp > 0) ? temp : 0;

	// in any case untrip_temp should always be at least trip_temp + hysteresis/2. (if untrip < (trip + hyst/2) => untrip = trip + hyst/2)
	temp = (boiler->set.hysteresis/2) - (untrip_temp - trip_temp);
	untrip_temp += ((tempdiff_t)temp > 0) ? temp : 0;

	// cap untrip temp at limit_tmax
	if (untrip_temp > boiler->set.limit_tmax)
		untrip_temp = boiler->set.limit_tmax;

	// return value within hysteresis
	ret = ALL_OK;

	/* burner control */
	now = timekeep_now();
	elapsed = now - boiler->run.burner_1_last_switch;
	// cooldown is applied to both turn-on and turn-off to avoid pumping effect that could damage the burner - state_get() is assumed not to fail
	if ((actual_temp < trip_temp) && !outputs_relay_state_get(boiler->set.rid_burner_1)) {		// trip condition
		if (elapsed >= boiler->set.burner_min_time) {	// cooldown start
			ret = outputs_relay_state_set(boiler->set.rid_burner_1, ON);
			boiler->run.burner_1_last_switch = now;
		}
	}
	else if ((actual_temp > untrip_temp) && outputs_relay_state_get(boiler->set.rid_burner_1)) {	// untrip condition
		if ((elapsed >= boiler->set.burner_min_time) || (actual_temp > boiler->set.limit_tmax)) {	// delayed stop - except if we're maxing out
			ret = outputs_relay_state_set(boiler->set.rid_burner_1, OFF);
			boiler->run.burner_1_last_switch = now;
		}
	}

	if (unlikely(ALL_OK != ret)) {
		alarms_raise(ret, _("Boiler \"%s\": burner control failed!"), heat->name);
		goto fail;
	}

	// ret is now ALL_OK until proven otherwise

	// computations performed while burner is on
	if (outputs_relay_state_get(boiler->set.rid_burner_1) > 0) {
		// if boiler temp is > limit_tmin, as long as the burner is running we reset the cooldown delay
		if (boiler->set.limit_tmin < actual_temp)
			heat->run.target_consumer_sdelay = heat->set.consumer_sdelay;
		// otherwise if boiler doesn't heat up after 6h we very likely have a problem
		else if (unlikely((now - boiler->run.burner_1_last_switch) > timekeep_sec_to_tk(3600*6))) {
			ret = -EGENERIC;
			alarms_raise(ret, _("Boiler \"%s\": Burner failure, no heat output after 6h"), heat->name);
		}

		// compute turn-on anticipation for next run
		if (temp_deriv < 0) {
			if (!boiler->run.negderiv_starttime) {
				boiler->run.turnon_negderiv = temp_deriv;
				boiler->run.negderiv_starttime = now;
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
				// LIB_DERIV_FPDEC==0x8000 is good for up to 3,5h burner run time if TIMEKEEP_SMULT==10
				boiler->run.turnon_next_adj = lib_fpdiv_u32((now - boiler->run.negderiv_starttime), (unsigned)-boiler->run.turnon_negderiv, LIB_DERIV_FPDEC);
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
	(void)!inputs_temperature_get(boiler->set.tid_boiler_return, &temp);
	dbgmsg(1, 1, "\"%s\": on: %d, hrq_t: %.1f, tg_t: %.1f, cr_t: %.1f, trip_t: %.1f, untrip_t: %.1f, ret: %.1f, deriv: %d, curradj: %d",
	       heat->name, outputs_relay_state_get(boiler->set.rid_burner_1), temp_to_celsius(aler(&heat->run.temp_request)), temp_to_celsius(target_temp),
	       temp_to_celsius(actual_temp), temp_to_celsius(trip_temp), temp_to_celsius(untrip_temp), temp_to_celsius(temp), temp_deriv, boiler->run.turnon_curr_adj);
#endif

	return (ret);

fail:
	boiler_failsafe(boiler);
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

	heat->cb.log_reg = boiler_hscb_log_register;
	heat->cb.log_dereg = boiler_hscb_log_deregister;
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

