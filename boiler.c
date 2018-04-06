//
//  boiler.c
//  rwchcd
//
//  (C) 2017-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>

#include "boiler.h"
#include "pump.h"
#include "lib.h"
#include "hardware.h"
#include "alarms.h"

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

	// check that mandatory sensors are working
	ret = hardware_sensor_clone_temp(boiler->set.id_temp, NULL);
	if (ALL_OK != ret)
		alarms_raise(ret, _("Boiler sensor failure"), _("Boiler sens fail"));

	return (ret);
}

/**
 * Create a new boiler.
 * @return pointer to the created boiler
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
 * @param boiler the boiler to delete
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

	hardware_sensor_clone_temp(boiler->set.id_temp, &temp);

	return (temp);
}

static time_t boiler_hscb_time(struct s_heatsource * const heat)
{
	const struct s_boiler_priv * const boiler = heat->priv;
	time_t ttime;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	hardware_sensor_clone_time(boiler->set.id_temp, &ttime);

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

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	// check that mandatory sensors are set
	ret = hardware_sensor_clone_time(boiler->set.id_temp, NULL);
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!boiler->set.limit_tmax)
		ret = -EMISCONFIGURED;

	// check that hardmax is > tmax (effectively checks that it's set too)
	if (boiler->set.limit_thardmax < boiler->set.limit_tmax)
		ret = -EMISCONFIGURED;

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

	// reset integral
	boiler->run.boil_itg.last_time = 0;

	hardware_relay_set_state(boiler->set.rid_burner_1, OFF, 0);
	hardware_relay_set_state(boiler->set.rid_burner_2, OFF, 0);

	if (boiler->loadpump)
		pump_offline(boiler->loadpump);

	return (ALL_OK);
}

/**
 * Safety routine to apply to boiler in case of emergency.
 * @param boiler target boiler
 */
static void boiler_failsafe(struct s_boiler_priv * const boiler)
{
	hardware_relay_set_state(boiler->set.rid_burner_1, OFF, 0);
	hardware_relay_set_state(boiler->set.rid_burner_2, OFF, 0);
	// failsafe() is called after runchecklist(), the above can't fail

	if (boiler->loadpump)
		pump_set_state(boiler->loadpump, ON, FORCE);
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

	hardware_sensor_clone_temp(boiler->set.id_temp, &boilertemp);
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
 * @todo burner turn-on anticipation
 */
static int boiler_hscb_logic(struct s_heatsource * restrict const heat)
{
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t target_temp = RWCHCD_TEMP_NOREQUEST;
	int ret;

	assert(HS_BOILER == heat->set.type);
	assert(boiler);

	// safe operation check
	ret = boiler_runchecklist(boiler);
	if (ALL_OK != ret) {
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
	if (boiler->run.antifreeze)
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
 * @note As a special case in the plant, antifreeze takes over all states if the boiler is configured (and online).
 * @note cold startup protection has a hardcoded 2% per 1Ks ratio
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler)
 * @warning no parameter check
 * @todo XXX TODO: implement 2nd stage (p.51)
 * @todo XXX TODO: implement limit on return temp (p.55/56 / p87-760), (consummer shift / return valve / bypass pump)
 * @todo review integral jacketing - maybe use a PI(D) instead?
 */
static int boiler_hscb_run(struct s_heatsource * const heat)
{
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t boiler_temp, trip_temp, untrip_temp, temp_intgrl, temp;
	time_t ttime;
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

	// check we can run
	ret = boiler_runchecklist(boiler);
	if (ALL_OK != ret) {
		boiler_failsafe(boiler);
		return (ret);
	}

	ret = hardware_sensor_clone_temp(boiler->set.id_temp, &boiler_temp);

	// ensure boiler is within safety limits
	if ((ALL_OK != ret) || (boiler_temp > boiler->set.limit_thardmax)) {
		boiler_failsafe(boiler);
		heat->run.cshift_crit = RWCHCD_CSHIFT_MAX;
		return (-ESAFETY);
	}

	// we're good to go

	/* todo handle return temp limit (limit low only for boiler):
	 * if a return mixing valve is available, use it, else form a critical
	 * shift signal. Consider handling of loadpump */

	// calculate boiler integral
	hardware_sensor_clone_time(boiler->set.id_temp, &ttime);
	temp_intgrl = temp_thrs_intg(&boiler->run.boil_itg, boiler->set.limit_tmin, boiler_temp, ttime);

	// form consumer shift request if necessary for cold start protection
	if (temp_intgrl < 0) {
		// at boiler first start the integral can windup quickly: jacket integral at -200% - XXX hardcoded
		if (temp_intgrl < (-100 * KPRECISIONI))
			boiler->run.boil_itg.integral = temp_intgrl = (-100 * KPRECISIONI);

		// percentage of shift is formed by the integral of current temp vs expected temp: 1Ks is -2% shift - XXX hardcoded
		heat->run.cshift_crit = 2 * temp_intgrl / KPRECISIONI;
		dbgmsg("\"%s\": integral: %d mKs, cshift_crit: %d%%", heat->name, temp_intgrl, heat->run.cshift_crit);
	}
	else {
		heat->run.cshift_crit = 0;		// reset shift
		boiler->run.boil_itg.integral = 0;	// reset integral
	}

	// turn pump on if any
	if (boiler->loadpump)
		pump_set_state(boiler->loadpump, ON, 0);

	/* un/trip points */
	// apply trip_temp only if we have a heat request
	if (RWCHCD_TEMP_NOREQUEST != boiler->run.target_temp) {
		trip_temp = (boiler->run.target_temp - boiler->set.hysteresis/2);
		if (trip_temp < boiler->set.limit_tmin)
			trip_temp = boiler->set.limit_tmin;
	}
	else
		trip_temp = 0;

	// always apply untrip temp (stop condition must always exist)
	untrip_temp = (boiler->run.target_temp + boiler->set.hysteresis/2);

	// operate at constant hysteresis on the low end
	untrip_temp += (boiler->set.hysteresis - (untrip_temp - trip_temp));

	// allow shifting untrip temp if actual heat request goes below trip_temp (e.g. when trip_temp = limit_tmin)...
	temp = trip_temp - heat->run.temp_request;
	untrip_temp -= (temp > 0) ? temp : 0;

	// ... up to hysteresis/2 (if untrip < (trip + hyst/2) => untrip = trip + hyst/2)
	temp = (boiler->set.hysteresis/2) - (untrip_temp - trip_temp);
	untrip_temp += (temp > 0) ? temp : 0;

	// cap untrip temp at limit_tmax
	if (untrip_temp > boiler->set.limit_tmax)
		untrip_temp = boiler->set.limit_tmax;

	/* burner control */
	// cooldown is applied to both turn-on and turn-off to avoid pumping effect that could damage the burner
	if (boiler_temp < trip_temp)		// trip condition
		ret = hardware_relay_set_state(boiler->set.rid_burner_1, ON, boiler->set.burner_min_time);	// cooldown start
	else if (boiler_temp > untrip_temp)	// untrip condition
		ret = hardware_relay_set_state(boiler->set.rid_burner_1, OFF, boiler->set.burner_min_time);	// delayed stop

	// as long as the burner is running we reset the cooldown delay
	if (hardware_relay_get_state(boiler->set.rid_burner_1) > 0)
		heat->run.target_consumer_sdelay = heat->set.consumer_sdelay;

	dbgmsg("\"%s\": on: %d, hrq_t: %.1f, tg_t: %.1f, cr_t: %.1f, trip_t: %.1f, untrip_t: %.1f",
	       heat->name, hardware_relay_get_state(boiler->set.rid_burner_1), temp_to_celsius(heat->run.temp_request), temp_to_celsius(boiler->run.target_temp),
	       temp_to_celsius(boiler_temp), temp_to_celsius(trip_temp), temp_to_celsius(untrip_temp));

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

	if (heat->priv)
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
