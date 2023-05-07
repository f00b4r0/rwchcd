//
//  plant/dhwt.c
//  rwchcd
//
//  (C) 2017-2022 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT operation implementation.
 *
 * The DHWT implementation supports:
 * - boiler-integrated tanks (by setting temp_inoffset to a near-zero value, assuming the boiler temp equals the DHWT temp; and making sure the chosen target temp and hysteresis align with the settings of the heatsource).
 * - automatic switch-over to (optional) integrated electric-heating (in summer or when heatsource failed).
 * - single and dual sensor operation (top/bottom) with adaptive hysteresis strategies.
 * - adaptive heatsource feed temperature management based on current DHWT temperature.
 * - timed feedpump cooldown at untrip with temperature discharge protection.
 * - 5 charge priority models (no priority, parallel or absolute; with heat request selection).
 * - forced manual charge.
 * - 3 RM_COMFORT mode charge forcing models (never force charge, force first charge of the day, force all comfort charges).
 * - charge duration cap.
 * - DHW recycling pump.
 * - DHWT isolation valve.
 * - min/max limits on DHW temperature.
 * - maximum intake temperature limit.
 * - periodic anti-legionella high heat charge.
 * - feed isolation valve.
 * - individual scheduling.
 * - logging of state and temperatures.
 * - summer maintenance of actuators when operating in frostfree mode.
 * - 1K hysteresis on overtemp condition (triggered by reaching limit_tmax)
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * no concurrent operation is ever expected to happen to a given dhwt, with the exception of
 * logging activity for which only data races are prevented via relaxed operations.
 * It is worth noting that no data consistency is guaranteed for logging, i.e. the data points logged
 * during a particular call of dhwt_logdata_cb() may represent values from different time frames:
 * the overhead of ensuring consistency seems overkill for the purpose served by the log facility.
 *
 * @note during test / summer maintenance, if the DHWT features a feed isolation valve, the feed
 * pump turn on will be delayed until that valve is fully opened. Likewise, the DHW recycle pump will
 * be delayed until the DHW isolation valve is open, if present.
 *
 * @note the threshold logic on DHW isolation valve allows as a side effect the following setup to operate correctly:
 * Boiler integrated DHW tank + external electric-only DHWT (with own thermostat) + switchover valve. Suitable DHWT configuration:
 * - near-zero temp_inoffset
 * - electric heater relay controlling external electric DHWT
 * - electric_hasthermostat set to true
 * - tthresh_dhwisol set to a low enough temperature that is only reached when boiler is powered off (e.g. 35°C)
 * - valve_dhwisol trigger controlling switchover valve: when open integrated DHWT is used, when closed electric DHWT is used
 * Result: when plant_could_sleep is asserted, the electric heated DHWT will be turned on. As the boiler cools down, the switchover valve flips.
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
#include "log/log.h"
#include "alarms.h"
#include "dhwt_priv.h"

#define DHWT_STORAGE_PREFIX	"dhwt"

/**
 * DHWT data log callback.
 * @param ldata the log data to populate
 * @param object the opaque pointer to dhwt structure
 * @return exec status
 */
static int dhwt_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_dhwt * const dhwt = object;
	unsigned int i = 0;

	assert(ldata);
	assert(ldata->nkeys >= 8);

	if (!dhwt)
		return (-EINVALID);

	if (!aler(&dhwt->run.online))
		return (-EOFFLINE);

	ldata->values[i++].i = aler(&dhwt->run.runmode);
	ldata->values[i++].i = aler(&dhwt->run.charge_on);
	ldata->values[i++].i = aler(&dhwt->run.recycle_on);
	ldata->values[i++].i = aler(&dhwt->run.force_on);
	ldata->values[i++].i = aler(&dhwt->run.legionella_on);
	ldata->values[i++].i = aler(&dhwt->run.electric_mode);
	ldata->values[i++].f = temp_to_celsius(aler(&dhwt->run.target_temp));
	ldata->values[i++].f = temp_to_celsius(aler(&dhwt->run.actual_temp));

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for a given dhwt.
 * @param dhwt the target dhwt
 * @return (statically allocated) s_log_source pointer
 * @warning must not be called concurrently
 */
static const struct s_log_source * dhwt_lsrc(const struct s_dhwt * const dhwt)
{
	static const log_key_t keys[] = {
		"runmode", "charge_on", "recycle_on", "force_on", "legionella_on", "electric_mode", "target_temp", "actual_temp",
	};
	static const enum e_log_metric metrics[] = {
		LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_IGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE,
	};
	const log_version_t version = 1;
	static struct s_log_source Dhwt_lsrc;

	Dhwt_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_1mn,
		.basename = DHWT_STORAGE_PREFIX,
		.identifier = dhwt->name,
		.version = version,
		.logdata_cb = dhwt_logdata_cb,
		.nkeys = ARRAY_SIZE(keys),
		.keys = keys,
		.metrics = metrics,
		.object = dhwt,
	};
	return (&Dhwt_lsrc);
}

/**
 * Register a dhwt for logging.
 * @param dhwt the target dhwt
 * @return exec status
 */
static int dhwt_log_register(const struct s_dhwt * const dhwt)
{
	assert(dhwt);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	if (!dhwt->set.log)
		return (ALL_OK);

	return (log_register(dhwt_lsrc(dhwt)));
}

/**
 * Deregister a dhwt from logging.
 * @param dhwt the target dhwt
 * @return exec status
 */
static int dhwt_log_deregister(const struct s_dhwt * const dhwt)
{
	assert(dhwt);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	if (!dhwt->set.log)
		return (ALL_OK);

	return (log_deregister(dhwt_lsrc(dhwt)));
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
	int ret;

	if (!dhwt)
		return (-EINVALID);

	assert(dhwt->pdata);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are set
	ret = inputs_temperature_get(dhwt->set.tid_bottom, NULL);
	if (ALL_OK != ret)
		ret = inputs_temperature_get(dhwt->set.tid_top, NULL);
	if (ALL_OK != ret) {
		pr_err(_("\"%s\": both tid_bottom and tid_top failed, need at least one!"), dhwt->name);
		ret = -EMISCONFIGURED;
	}

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
		temp = SETorDEF(dhwt->set.params.t_legionella, dhwt->pdata->set.def_dhwt.t_legionella);
		if (!temp) {
			pr_err(_("\"%s\": anti_legionella is set: t_legionella must be locally or globally > 0°K!"), dhwt->name);
			ret = -EMISCONFIGURED;
		}
		else if (temp > SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax)) {
			pr_err(_("\"%s\": anti_legionella is set: t_legionella must be locally or globally <= limit_tmax"), dhwt->name);
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

	// if pumps exist check they're available
	if (dhwt->set.p.pump_feed) {
		if (!pump_is_online(dhwt->set.p.pump_feed)) {
			pr_err(_("\"%s\": pump_feed \"%s\" is set but not online"), dhwt->name, pump_name(dhwt->set.p.pump_feed));
			ret = -EMISCONFIGURED;
		}
		// make sure we have win sensor
		if (inputs_temperature_get(dhwt->set.tid_win, NULL) != ALL_OK) {
			pr_err(_("\"%s\": tid_win failed or missing: needed with feed pump!"), dhwt->name);
			ret = -EMISCONFIGURED;
		}
	}

	if (dhwt->set.p.pump_dhwrecycle && !pump_is_online(dhwt->set.p.pump_dhwrecycle)) {
		pr_err(_("\"%s\": pump_dhwrecycle \"%s\" is set but not online"), dhwt->name, pump_name(dhwt->set.p.pump_dhwrecycle));
		ret = -EMISCONFIGURED;
	}

	if (dhwt->set.p.valve_feedisol) {
		if (!valve_is_online(dhwt->set.p.valve_feedisol)) {
			pr_err(_("\"%s\": valve_feedisol \"%s\" is set but not configured"), dhwt->name, valve_name(dhwt->set.p.valve_feedisol));
			ret = -EMISCONFIGURED;
		}
		else if (VA_TYPE_ISOL != valve_get_type(dhwt->set.p.valve_feedisol)) {
			pr_err(_("\"%s\": Invalid type for valve_feedisol \"%s\" (isolation valve expected)"), dhwt->name, valve_name(dhwt->set.p.valve_feedisol));
			ret = -EMISCONFIGURED;
		}
	}

	if (dhwt->set.p.valve_dhwisol) {
		if (!valve_is_online(dhwt->set.p.valve_dhwisol)) {
			pr_err(_("\"%s\": valve_dhwisol \"%s\" is set but not configured"), dhwt->name, valve_name(dhwt->set.p.valve_dhwisol));
			ret = -EMISCONFIGURED;
		}
		else if (VA_TYPE_ISOL != valve_get_type(dhwt->set.p.valve_dhwisol)) {
			pr_err(_("\"%s\": Invalid type for valve_dhwisol \"%s\" (isolation valve expected)"), dhwt->name, valve_name(dhwt->set.p.valve_dhwisol));
			ret = -EMISCONFIGURED;
		}
	}

	// warn on unenforceable configuration
	if (dhwt->set.tthresh_dhwisol) {
		if (!(dhwt->set.p.valve_dhwisol || dhwt->set.p.pump_dhwrecycle))
			pr_warn(_("\"%s\": tthresh_dhwisol set but neither pump_dhwrecycle nor valve_dhwisol set: ignored."), dhwt->name);
	}

	// grab relay as needed
	if (outputs_relay_name(dhwt->set.rid_selfheater)) {
		if (outputs_relay_grab(dhwt->set.rid_selfheater) != ALL_OK) {
			pr_err(_("\"%s\": Relay for self-heater is unavailable"), dhwt->name);
			ret = -EMISCONFIGURED;
		}
	}

	if (ALL_OK == ret) {
		aser(&dhwt->run.online, true);

		// log registration shouldn't cause onlining to fail
		if (dhwt_log_register(dhwt) != ALL_OK)
			pr_err(_("\"%s\": couldn't register for logging"), dhwt->name);
	}

	return (ret);
}

/**
 * Shutdown dhwt.
 * Perform all necessary actions to shut down the dhwt.
 * @param dhwt target dhwt
 * @return error status
 */
static int dhwt_shutdown(struct s_dhwt * const dhwt)
{
	assert(dhwt);
	assert(dhwt->set.configured);

	if (!dhwt->run.active)
		return (ALL_OK);

	// clear runtime data while preserving online state
	aser(&dhwt->run.charge_on, false);
	aser(&dhwt->run.recycle_on, false);
	aser(&dhwt->run.force_on, false);
	aser(&dhwt->run.legionella_on, false);
	dhwt->run.charge_overtime = false;
	aser(&dhwt->run.electric_mode, false);
	dhwt->run.mode_since = 0;	// XXX
	dhwt->run.charge_yday = 0;	// XXX

	dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	aser(&dhwt->run.target_temp, 0);

	(void)!outputs_relay_state_set(dhwt->set.rid_selfheater, OFF);

	if (dhwt->set.p.pump_feed)
		pump_shutdown(dhwt->set.p.pump_feed);

	if (dhwt->set.p.pump_dhwrecycle)
		pump_shutdown(dhwt->set.p.pump_dhwrecycle);

	// isolate DHWT if possible
	if (dhwt->set.p.valve_feedisol)
		(void)!valve_isol_trigger(dhwt->set.p.valve_feedisol, true);

	if (dhwt->set.p.valve_dhwisol)
		(void)!valve_isol_trigger(dhwt->set.p.valve_dhwisol, true);

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
	dhwt_log_deregister(dhwt);

	outputs_relay_thaw(dhwt->set.rid_selfheater);

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
	const struct s_schedule_eparams * eparams;
	const time_t tnow = time(NULL);
	const enum e_systemmode sysmode = runtime_systemmode();
	enum e_runmode prev_runmode, new_runmode;
	temp_t target_temp, ltmin, ltmax;
	bool recycle = false;
	struct tm ltime;

	assert(dhwt);

	localtime_r(&tnow, &ltime);	// localtime handles DST and TZ for us

	// store current status for transition detection
	prev_runmode = aler(&dhwt->run.runmode);

	// SYS_TEST/SYS_OFF always override
	if ((SYS_TEST == sysmode) || (SYS_OFF == sysmode))
		new_runmode = runtime_dhwmode();
	else {
		// handle global/local runmodes
		new_runmode = aler(&dhwt->overrides.o_runmode) ? aler(&dhwt->overrides.runmode) : dhwt->set.runmode;
		if (RM_AUTO == new_runmode) {
			if (runtime_get_stopdhw())	// if killswitch is engaged, stop
				new_runmode = RM_FROSTFREE;
			else {
				// if we have a schedule, use it, or global settings if unavailable
				eparams = scheduler_get_schedparams(dhwt->set.schedid);
				if ((SYS_AUTO == sysmode) && eparams) {
					new_runmode = eparams->dhwmode;
					aser(&dhwt->run.legionella_on, eparams->legionella);
					// XXX REVISIT recycle can only be set via schedule for now
					if (dhwt->set.p.pump_dhwrecycle)
						recycle = aler(&dhwt->run.electric_mode) ? (eparams->recycle && dhwt->set.electric_recycle) : eparams->recycle;
				}
				else	// don't touch legionella
					new_runmode = runtime_dhwmode();
			}
		}
	}

	// depending on dhwt run mode, assess dhwt target temp
	switch (new_runmode) {
		case RM_OFF:
		case RM_TEST:
			aser(&dhwt->run.runmode, new_runmode);
			return (ALL_OK);	// No further processing
		case RM_ECO:
			if (!aler(&dhwt->run.electric_mode)) {
				target_temp = SETorDEF(dhwt->set.params.t_eco, dhwt->pdata->set.def_dhwt.t_eco);
				break;
			}
			// fallthrough - we don't support eco on electric due to expected inertia
		case RM_COMFORT:
			target_temp = SETorDEF(dhwt->set.params.t_comfort, dhwt->pdata->set.def_dhwt.t_comfort);
			break;
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		case RM_SUMMAINT:
		default:
			dbgerr("\"%s\": invalid runmode (%d), falling back to RM_FROSTREE", dhwt->name, new_runmode);
			new_runmode = RM_FROSTFREE;
			// fallthrough
		case RM_FROSTFREE:
			target_temp = SETorDEF(dhwt->set.params.t_frostfree, dhwt->pdata->set.def_dhwt.t_frostfree);
			if (dhwt->pdata->run.summer_maint)
				new_runmode = RM_SUMMAINT;
			break;
	}

	// if anti-legionella charge is requested and allowed, enforce temp and bypass logic
	if (unlikely(aler(&dhwt->run.legionella_on))) {
		if (dhwt->set.anti_legionella) {
			target_temp = SETorDEF(dhwt->set.params.t_legionella, dhwt->pdata->set.def_dhwt.t_legionella);
			aser(&dhwt->run.force_on, true);
			recycle = dhwt->set.legionella_recycle;
			goto settarget;
		}
		else
			aser(&dhwt->run.legionella_on, false);
	}

	// transition detection
	if (unlikely(prev_runmode != new_runmode)) {
		// handle programmed forced charges at COMFORT switch on
		if (RM_COMFORT == new_runmode) {
			if (DHWTF_ALWAYS == dhwt->set.force_mode)
				aser(&dhwt->run.force_on, true);
			else if ((DHWTF_FIRST == dhwt->set.force_mode) && (ltime.tm_yday != dhwt->run.charge_yday)) {
				aser(&dhwt->run.force_on, true);
				dhwt->run.charge_yday = ltime.tm_yday;
			}
		}
	}

settarget:
	// enforce limits on dhw temp
	ltmin = SETorDEF(dhwt->set.params.limit_tmin, dhwt->pdata->set.def_dhwt.limit_tmin);
	ltmax = SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax);
	if (target_temp < ltmin)
		target_temp = ltmin;
	else if (target_temp > ltmax)
		target_temp = ltmax;

	aser(&dhwt->run.recycle_on, recycle);

	// save current target dhw temp
	aser(&dhwt->run.target_temp, target_temp);
	aser(&dhwt->run.runmode, new_runmode);

	return (ALL_OK);
}

/**
 * DHWT failsafe routine.
 * By default we shutdown the tank. If configured for
 * electric failover the self-heater is still turned on unconditionnally
 * (this assumes that the self-heater has a local thermostat, which should always be the case).
 * The major inconvenient here is that this failsafe mode COULD provoke a DHWT
 * freeze in the most adverse conditions.
 * @warning DHWT could freeze if not electric self-heater is available
 * @param dhwt target dhwt
 */
static void dhwt_failsafe(struct s_dhwt * restrict const dhwt)
{
	int ret;

	assert(dhwt);

	dbgerr("\"%s\": failsafe mode!", dhwt->name);

	dhwt_shutdown(dhwt);

	ret = outputs_relay_state_set(dhwt->set.rid_selfheater, dhwt->set.electric_hasthermostat ? ON : OFF);
	if (ALL_OK == ret) {
		aser(&dhwt->run.electric_mode, dhwt->set.electric_hasthermostat);
		aser(&dhwt->run.charge_on, dhwt->set.electric_hasthermostat);
	}
}

/**
 * Check if DHWT water inlet temperature is acceptable.
 * Temperature is acceptable if all these conditions are met:
 * - it is >= DHWT current temperature + 1K (hysteresis);
 * - it is <= DHWT maximum permissible inlet temperature
 * @param dhwt target DHWT
 * @return 1 if temperature is acceptable, 0 if it is in the hysteresis region, -1 in all other cases (including sensor failure)
 */
static int dhwt_wintemp_acceptable(struct s_dhwt * restrict const dhwt)
{
	const temp_t win_tmax = SETorDEF(dhwt->set.params.limit_wintmax, dhwt->pdata->set.def_dhwt.limit_wintmax);
	const temp_t curr_temp = aler(&dhwt->run.actual_temp);
	temp_t win_temp;
	int ret;

	/// @warning Note: tid_win sensor must not rely on pump running for accurate read, otherwise this can be a problem
	ret = inputs_temperature_get(dhwt->set.tid_win, &win_temp);
	if (unlikely(ALL_OK != ret)) {
		alarms_raise(ret, _("DHWT \"%s\": failed to get inlet temperature!"), dhwt->name);
		return -1;
	}

	ret = -1;
	if (win_temp <= win_tmax) {
		if (win_temp >= (curr_temp + deltaK_to_temp(1)))
			ret = 1;
		else if (win_temp >= curr_temp)
			ret = 0;
	}

	return (ret);
}

/**
 * DHWT test / summer maintenance routine.
 * The only difference between test and summer maintenance is that the former triggers the electric heating (if any).
 * For safety purposes, the test stops if/when the tank temperature reaches the configured maximum.
 * @param dhwt target dhwt
 * @param runmode target runmode (only RM_TEST and RM_SUMMAINT allowed)
 * @return exec status
 * @note no check on inlet temperature is performed
 */
static int dhwt_run_testsummaint(struct s_dhwt * restrict const dhwt, enum e_runmode runmode)
{
	const temp_t tmax = SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax);
	bool test;

	assert((RM_TEST == runmode) || (RM_SUMMAINT == runmode));

	if (aler(&dhwt->run.actual_temp) >= tmax)
		return (dhwt_shutdown(dhwt));		// stop the test if we've reached max temp

	if (RM_TEST == runmode)
		(void)!outputs_relay_state_set(dhwt->set.rid_selfheater, ON);

	// NB: we do enable the isolation valve in summaint because it makes no sense to turn the
	// feedpump on if there is an isolation valve and it's closed. If we reached this point we
	// aren't operating on electric heater anyway.
	dhwt->run.active = true;
	dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	if (dhwt->set.p.valve_feedisol) {
		(void)!valve_isol_trigger(dhwt->set.p.valve_feedisol, false);
		// if we have a feed isolation valve, it must be open before turning on the feedpump
		test = valve_is_open(dhwt->set.p.valve_feedisol) ? ON : OFF;
	}
	else
		test = ON;
	if (dhwt->set.p.pump_feed)
		(void)!pump_set_state(dhwt->set.p.pump_feed, test, NOFORCE);	// don't force off (for shared pumps)

	if (dhwt->set.p.valve_dhwisol) {
		(void)!valve_isol_trigger(dhwt->set.p.valve_dhwisol, false);
		// if we have a dhw isolation valve, it must be open before turning on the recycle pump
		test = valve_is_open(dhwt->set.p.valve_dhwisol) ? ON : OFF;
	}
	else
		test = ON;
	if (dhwt->set.p.pump_dhwrecycle)
		(void)!pump_set_state(dhwt->set.p.pump_dhwrecycle, test, NOFORCE);	// don't force off (for shared pumps)

	return (ALL_OK);
}

/**
 * DHWT domestic hot water isolation valve operation.
 * Adjusts the state of the tank DHW isolation valve based on the following pseudo-code logic:
 @verbatim
 if no thresh: open
 else:
	if curr_temp < threshold: close
	else if: curr_temp > threshold + hyst: open
	else (hysteresis deadband): do nothing
 @endverbatim
 * @param dhwt target DHWT
 * @return exec status
 */
static int dhwt_run_dhwisol(struct s_dhwt * restrict const dhwt)
{
	const temp_t thresh = dhwt->set.tthresh_dhwisol;
	const temp_t curr_temp = aler(&dhwt->run.actual_temp);
	bool isolate = false;
	int ret = ALL_OK;

	if (!thresh)
		goto set;

	if (curr_temp < thresh)
		isolate = true;
	else if (curr_temp < (thresh + deltaK_to_temp(1)))
		goto out;	// deadband
	// else we are above threshold: open

set:
	ret = valve_isol_trigger(dhwt->set.p.valve_dhwisol, isolate);
	if (unlikely(ALL_OK != ret))
		alarms_raise(ret, _("DHWT \"%s\": failed to control DHW isolation valve \"%s\""), dhwt->name, valve_name(dhwt->set.p.valve_dhwisol));
out:
	return ret;
}

/**
 * DHWT feed isolation valve operation.
 * Adjusts the state of the tank feed isolation valve based on the following pseudo-code logic:
 @verbatim
 if (electric || overtemp): close
 else
	if hs_overtemp: open (XXX tolerate that win may be > wintmax in this emergency case)
	else if (charge_on || floor_intake):
		if wintemp acceptable: open
		else if wintemp unacceptable: close
		else (hysteresis): do nothing
	else: close
 @endverbatim
 * @param dhwt target DHWT
 * @return exec status
 * @note assumes that hs_overtemp cannot happen in electric_mode (i.e. electric_mode will be turned off)
 */
static int dhwt_run_feedisol(struct s_dhwt * restrict const dhwt)
{
	bool isolate = true;
	int ret;

	if (aler(&dhwt->run.electric_mode) || dhwt->run.overtemp)
		goto set;

	if (dhwt->pdata->run.hs_overtemp)
		isolate = false;
	else if ((aler(&dhwt->run.charge_on) || dhwt->run.floor_intake)) {
		ret = dhwt_wintemp_acceptable(dhwt);
		if (!ret)	// dead zone - do nothing
			goto out;
		isolate = (ret != 1);	// isolate if temp not acceptable
	}

set:
	ret = valve_isol_trigger(dhwt->set.p.valve_feedisol, isolate);
	if (unlikely(ALL_OK != ret))
		alarms_raise(ret, _("DHWT \"%s\": failed to control feed isolation valve \"%s\""), dhwt->name, valve_name(dhwt->set.p.valve_feedisol));
out:
	return ret;
}

/**
 * DHWT feed pump operation.
 * Adjusts the state of the tank feedpump based on the following pseudo-code logic:
 @verbatim
 if (electric || overtemp): (feedisol ? soft : hard) off
 else:
 	if hs_overtemp: soft on (XXX tolerate that win may be > wintmax in this emergency case)
	else if (charge_on || floor_intake):
 		if wintemp acceptable: soft on
 		else if wintemp not acceptable : (feedisol ? soft : hard) off
 		else (hysteresis): do nothing
 	else (stop):
 		(feedisol || wintemp acceptable ? soft : hard) off // feedisol takes care of wintemp acceptable
	if feedisol closed: override off
 @endverbatim
 * @param dhwt target DHWT
 * @return exec status
 * @note discharge protection will fail if the input sensor needs water flow
 * in the pump_feed. It is thus important to ensure that the water input temperature sensor
 * can provide a reliable reading even when the feedpump is off.
 * @note assumes that hs_overtemp cannot happen in electric_mode (i.e. electric_mode will be turned off)
 */
static int dhwt_run_feedpump(struct s_dhwt * restrict const dhwt)
{
	const bool has_feedisol = !!dhwt->set.p.valve_feedisol;
	bool turn_on, force;
	int ret;

	if (aler(&dhwt->run.electric_mode) || dhwt->run.overtemp) {
		turn_on = OFF;
		force = has_feedisol ? NOFORCE : FORCE;
		goto set;
	}

	ret = dhwt_wintemp_acceptable(dhwt);

	force = NOFORCE;
	if (dhwt->pdata->run.hs_overtemp)
		turn_on = ON;
	else if (aler(&dhwt->run.charge_on) || dhwt->run.floor_intake) {
		if (!ret)	// dead zone - do nothing
			goto out;

		turn_on = (ret == 1);	// turn on if wintemp is acceptable
		if (!turn_on)
			force = has_feedisol ? NOFORCE : FORCE;
	}
	else {
		turn_on = OFF;
		force = (has_feedisol || (ret >= 0)) ? NOFORCE : FORCE;
	}

	// override while feedisol is closed
	if (has_feedisol && !valve_is_open(dhwt->set.p.valve_feedisol))
		turn_on = OFF;

set:
	ret = pump_set_state(dhwt->set.p.pump_feed, turn_on, force);
	if (unlikely(ALL_OK != ret))
		alarms_raise(ret, _("DHWT \"%s\": failed to request feed pump \"%s\" state"), dhwt->name, pump_name(dhwt->set.p.pump_feed));
out:
	return ret;
}

/**
 * DHWT DHW recycle pump opration.
 * Currently very limited logic as follows:
 @verbatim
 if hs_overtemp: hard on
 else:
  	soft (recycle_on ? on : off)
	if ttresh_dhwisol:
		if curr_temp < threshold: override off
		else if: curr_temp > threshold + hyst: no override
		else (hysteresis deadband): do nothing
 *
 if dhwisol closed: override off
 @endverbatim
 * @param dhwt target DHWT
 * @return exec status
 */
static int dhwt_run_dhwrecyclepump(struct s_dhwt * restrict const dhwt)
{
	const temp_t thresh = dhwt->set.tthresh_dhwisol;
	const temp_t curr_temp = aler(&dhwt->run.actual_temp);
	bool turn_on, force;
	int ret = ALL_OK;

	if (dhwt->pdata->run.hs_overtemp) {
		turn_on = ON;
		force = FORCE;
	}
	else {
		force = NOFORCE;
		turn_on = aler(&dhwt->run.recycle_on) ? ON : OFF;
		if (thresh) {
			if (curr_temp < thresh)
				turn_on = OFF;	// override off
			else if (curr_temp < (thresh + deltaK_to_temp(1)))
				goto out;	// deadband, preserve current state
			// else we are above threshold: no override
		}
	}

	// override while dhwisol is closed
	if (dhwt->set.p.valve_dhwisol && !valve_is_open(dhwt->set.p.valve_dhwisol))
		turn_on = OFF;

	ret = pump_set_state(dhwt->set.p.pump_dhwrecycle, turn_on, force);
	if (unlikely(ALL_OK != ret))
		alarms_raise(ret, _("DHWT \"%s\": failed to request recycle pump \"%s\" state"), dhwt->name, pump_name(dhwt->set.p.pump_dhwrecycle));
out:
	return ret;
}

/**
 * DHWT heat request computation
 * Calculate necessary water feed temp: target tank temp + offset.
 * adjust requested feed temp offset based on the current temp of the DHWT:
 * we use the delta between current temp and target temp, *UP TO* the set offset.
 * This avoids requesting full offset (typically 10K) when the tank is nearly fully charged.
 * @param dhwt target DHWT
 * @param curr_temp current DHWT temp
 * @param target_temp DHWT target temp
 * @return a computed heat request temperature for this DHWT
 */
static temp_t dhwt_heat_request(struct s_dhwt * restrict const dhwt, const temp_t curr_temp, const temp_t target_temp)
{
	const temp_t wintmax = SETorDEF(dhwt->set.params.limit_wintmax, dhwt->pdata->set.def_dhwt.limit_wintmax);
	temp_t heat_req;

	heat_req = target_temp + min((target_temp - curr_temp), SETorDEF(dhwt->set.params.temp_inoffset, dhwt->pdata->set.def_dhwt.temp_inoffset));

	// enforce limits
	if (heat_req > wintmax)
		heat_req = wintmax;

	return (heat_req);
}

/**
 * DHWT charge update helper.
 * Ensure consistency of force_on and legionnella_on when charge is turned off.
 * @param dhwt target DHWT
 * @param charge_on new charge state
 * @param now current timestamp
 */
static inline void dhwt_update_charge(struct s_dhwt * restrict const dhwt, const bool charge_on, const timekeep_t now)
{
	if (!charge_on) {
		// untrip force charge: force can run only once
		aser(&dhwt->run.force_on, false);

		// untrip legionella if it was on
		aser(&dhwt->run.legionella_on, false);
	}
	dhwt->run.mode_since = now;
	aser(&dhwt->run.charge_on, charge_on);
}

/**
 * DHWT control loop.
 * Controls the dhwt's elements to achieve the desired target temperature.
 * If charge time exceeds the limit, the DHWT will be stopped for the duration
 * of the set limit.
 * Due to implementation in dhwt_failsafe() the DHWT can be configured to operate
 * purely on electric heating in the event of sensor failure, but this is still
 * considered a degraded operation mode and it will be reported as an error.
 * Operation logic pseudo code is as follows:
 @verbatim
  // attempt electric operation first if needed
  if try_electric && relay:
	 // switch on can only happen at trip, switch off happens at failure (!charge_on)
	 if no charge:
		 if trip point:
			 if electric relay set successful: set electric_mode / signal charge / shutdown water
			 else: set no electric_mode
		 else: nothing - state unchanged
	 else if electric_mode:	// ongoing electric charge
		 if !skip_untrip && (temp > target): turn off relay / signal no charge (but stay in electric mode)
		 else: nothing - state uchanged
	 else:	// ongoing water charge
		 nothing // let water code deal with it
  else:
	 turn off relay / unset electric_mode (let water operations resume)

  // fallback to water-based
  if !electric_mode:
	 if no charge:
		 if charge_overtime:
			 if last charge not too recent: relax
		 else if trip condition (temp ok && hs ok && prio ok): set charge
		 else: nothing
	 else:
		 if temp ok || hs failed || (!legionella && (overtime || prio nok)): untrip / set nocharge
		 else: keep updating heat request
 @endverbatim
 * @param dhwt target dhwt
 * @return error status
 * @note An ongoing anti-legionella charge will not be interrupted by a plant-wide change in priority or overtime.
 * @note Since anti-legionella can only be unset _after_ a complete charge (or a DHWT shutdown),
 * once the anti-legionella charge has been requested, it is @b guaranteed to happen,
 * although not necessarily at the planned time if there is delay in servicing the target DHWT priority.
 * @note this function ensures that in the event of an error, the dhwt is put in a failsafe state as defined in dhwt_failsafe().
 */
int dhwt_run(struct s_dhwt * const dhwt)
{
	temp_t top_temp, bottom_temp, curr_temp, trip_temp, target_temp;
	bool valid_ttop, valid_tbottom, charge_on, electric_mode, skip_untrip, try_electric, test;
	const timekeep_t now = timekeep_now();
	enum e_runmode dhwmode;
	orid_t rselfheater;
	timekeep_t limit;
	int ret;

	if (unlikely(!dhwt))
		return (-EINVALID);

	assert(dhwt->pdata);

	if (unlikely(!aler(&dhwt->run.online)))	// implies set.configured == true
		return (-EOFFLINE);

	ret = dhwt_logic(dhwt);
	if (unlikely(ALL_OK != ret))
		goto fail;

	skip_untrip = false;

	dhwmode = aler(&dhwt->run.runmode);

	// force dhwt ON during hs_overtemp condition
	if (unlikely(dhwt->pdata->run.hs_overtemp))
		dhwmode = RM_COMFORT;

	switch (dhwmode) {
		case RM_OFF:	// XXX NB we don't deal with flooring here because turning off DHWT should be a rare occurence and hs_overtemp will be there if anything goes wrong
			return (dhwt_shutdown(dhwt));
		case RM_COMFORT:
		case RM_ECO:
			skip_untrip = dhwt->set.electric_hasthermostat;
			break;
		case RM_FROSTFREE:
			break;
		case RM_TEST:
		case RM_SUMMAINT:
			return (dhwt_run_testsummaint(dhwt, dhwmode));
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			ret = -EINVALIDMODE;	// this can never happen due to fallback in _logic()
			goto fail;
	}

	// if we reached this point then the dhwt is active
	dhwt->run.active = true;

	// check which sensors are available
	ret = inputs_temperature_get(dhwt->set.tid_bottom, &bottom_temp);
	valid_tbottom = (ALL_OK == ret) ? true : false;
	ret = inputs_temperature_get(dhwt->set.tid_top, &top_temp);
	valid_ttop = (ALL_OK == ret) ? true : false;

	// no sensor available, give up
	if (unlikely(!valid_tbottom && !valid_ttop)) {
		alarms_raise(ret, _("DHWT \"%s\": no valid temperature available!"), dhwt->name);
		goto fail;
	}

	// We're good to go

	// assess overtemp status - prefer bottom (colder) temp if available - use hardcoded 1K hysteresis
	curr_temp = valid_tbottom ? bottom_temp : top_temp;
	trip_temp = SETorDEF(dhwt->set.params.limit_tmax, dhwt->pdata->set.def_dhwt.limit_tmax);
	if (unlikely(dhwt->run.overtemp)) {
		if (curr_temp <= (trip_temp - deltaK_to_temp(-1))) {
			dhwt->run.overtemp = false;
			pr_err(_("DHWT \"%s\": overtemp condition ended."), dhwt->name);
		}
	}
	else if (unlikely(curr_temp > trip_temp)) {
		dhwt->run.overtemp = true;
		pr_err(_("DHWT \"%s\": overtemp!"), dhwt->name);	// this is a "soft" error - printed only at onset
		// go on: the rest of the code will end the charge if overtemp anyway since target_temp is enforced <= limit_tmax
	}

	charge_on = aler(&dhwt->run.charge_on);
	target_temp = aler(&dhwt->run.target_temp);
	electric_mode = aler(&dhwt->run.electric_mode);
	try_electric = (dhwt->pdata->run.plant_could_sleep || dhwt->pdata->run.hs_allfailed) && !dhwt->pdata->run.hs_overtemp;

	// current temperature
	/* NOTE we try to enforce sensor position, it SEEMS desirable, so that the full tank capacity is used before triggering a charge.
	   apply hysteresis on logic: trip at target - hysteresis (preferably on top sensor), untrip at target (preferably on bottom sensor). */
	if (!charge_on)
		curr_temp = valid_ttop ? top_temp : bottom_temp;	// prefer top temp if available (trip charge when top is cold)
	else
		curr_temp = valid_tbottom ? bottom_temp : top_temp;	// prefer bottom temp if available (untrip charge when bottom is hot)

	aser(&dhwt->run.actual_temp, curr_temp);

	// set trip point to (target temp - hysteresis)
	if (aler(&dhwt->run.force_on) || (RM_FROSTFREE == dhwmode))
		trip_temp = target_temp - deltaK_to_temp(1);	// if forced charge or frostfree, force hysteresis at 1K
	else
		trip_temp = target_temp - SETorDEF(dhwt->set.params.hysteresis, dhwt->pdata->set.def_dhwt.hysteresis);

	// electric operation
	rselfheater = dhwt->set.rid_selfheater;
	if (try_electric && outputs_relay_name(rselfheater)) {
		if (!charge_on) {		// heat_request is necessarily off here
			electric_mode = true;	// immediately pretend we can do electric (to disable water-based processing)
			if (curr_temp < trip_temp) {
				if (ALL_OK == outputs_relay_state_set(rselfheater, ON)) {
					// the plant is sleeping and we have a configured self heater: use it
					charge_on = true;
					dhwt_update_charge(dhwt, charge_on, now);
				}
				else		// electric failure
					electric_mode = false;
			}
		}
		else if (electric_mode) {	// electric charge in progress
			if (!skip_untrip && (curr_temp >= target_temp)) {
				// stop self-heater (if any)
				(void)!outputs_relay_state_set(rselfheater, OFF);
				charge_on = false;
				dhwt_update_charge(dhwt, charge_on, now);
			}
		}
		// else non electric charge - fallthrough to water-based logic
	}
	else {
		// electric off
		(void)!outputs_relay_state_set(rselfheater, OFF);
		electric_mode = false;
	}

	aser(&dhwt->run.electric_mode, electric_mode);

	// water-based operation
	if (!electric_mode) {
		if (!charge_on) {
			if (dhwt->run.charge_overtime) {	// prevent charge "pumping", enforce delay between charges
				limit = SETorDEF(dhwt->set.params.limit_chargetime, dhwt->pdata->set.def_dhwt.limit_chargetime);
				if (!(limit && ((now - dhwt->run.mode_since) <= limit)))	// timeout expired
					dhwt->run.charge_overtime = false;	// reset status for next cycle
			}
			// no overtime, trip condition: temp ok && hs ok && prio ok
			else if ((curr_temp < trip_temp) && !dhwt->pdata->run.hs_allfailed && (dhwt->pdata->run.dhwt_currprio >= dhwt->set.prio)) {
				// apply heat request - refer bottom temp if available since this is what will be used for untripping
				dhwt->run.heat_request = dhwt_heat_request(dhwt, valid_tbottom ? bottom_temp : top_temp, target_temp);

				// mark heating in progress
				charge_on = true;
				dhwt_update_charge(dhwt, charge_on, now);
			}
		}
		else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
			test = false;	// untrip test conditions

			// if target reached, untrip or if heatsources failed, untrip (next run will retry electric or nothing)
			if ((curr_temp >= target_temp) || dhwt->pdata->run.hs_allfailed)
				test = true;
			// if no anti-legionella charge (never interrupt an anti-legionella charge):
			else if (!aler(&dhwt->run.legionella_on)) {
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

			if (test) {
				// clear heat request (ensures it's off at electric switchover)
				dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				charge_on = false;
				dhwt_update_charge(dhwt, charge_on, now);

				// handle heatsource flooring requests on untrip
				if (dhwt->pdata->run.consumer_sdelay)
					dhwt->run.floor_intake = true;
			}
			else	// keep updating heat request while !electric charge is in progress - curr_temp is bottom if available here
				dhwt->run.heat_request = dhwt_heat_request(dhwt, curr_temp, target_temp);
		}

		// reset flooring when consumer_sdelay is reset (assumes it will reach 0 before retriggering) - XXX handled only in water charge assuming flooring request cannot happen simultaneously with plant_could_sleep
		if (dhwt->run.floor_intake && !dhwt->pdata->run.consumer_sdelay)
			dhwt->run.floor_intake = false;
	}

	// handle valve_feedisol
	if (dhwt->set.p.valve_feedisol) {
		ret = dhwt_run_feedisol(dhwt);
		if (ALL_OK != ret)
			goto fail;
	}

	// handle feed pump
	if (dhwt->set.p.pump_feed) {
		ret = dhwt_run_feedpump(dhwt);
		if (ALL_OK != ret)
			goto fail;
	}

	// handle valve_dhwisol
	if (dhwt->set.p.valve_dhwisol)
		dhwt_run_dhwisol(dhwt);		// ignore failure

	// handle recycle loop
	if (dhwt->set.p.pump_dhwrecycle)
		dhwt_run_dhwrecyclepump(dhwt);	// ignore failure

	dbgmsg(1, 1, "\"%s\": on: %d, since: %u, elec: %d, tg_t: %.1f, bot_t: %.1f, top_t: %.1f, hrq_t: %.1f",
	       dhwt->name, charge_on, timekeep_tk_to_sec(dhwt->run.mode_since), electric_mode, temp_to_celsius(target_temp),
	       valid_tbottom ? temp_to_celsius(bottom_temp) : -273.0, valid_ttop ? temp_to_celsius(top_temp) : -273.0,
	       temp_to_celsius(dhwt->run.heat_request));

	return (ALL_OK);

fail:
	dhwt_failsafe(dhwt);
	return (ret);
}

/**
 * DHWT destructor.
 * Frees all dhwt-local resources
 * @param dhwt the dhwt to delete
 */
void dhwt_cleanup(struct s_dhwt * restrict dhwt)
{
	if (!dhwt)
		return;

	freeconst(dhwt->name);
	dhwt->name = NULL;
}
