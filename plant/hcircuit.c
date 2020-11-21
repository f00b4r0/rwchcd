//
//  plant/hcircuit.c
//  rwchcd
//
//  (C) 2017-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation implementation.
 *
 * The heating circuit operation implementation supports:
 * - Water-based circuits with radiators
 * - Per-circuit, independent target ambient temperature
 * - Per-circuit building model assignment
 * - Direct heating circuits
 * - Mixed heating circuits, with mixing valve:
 *     - Support for water temperature rate of rise control
 * - Multiple types of heating curves (linear and bilinear approximations are implemented)
 * - Ambient temperature modelisation in the absence of an ambient sensor
 * - Accelerated cooldown and boost warmup transitions
 * - Optional circuit ambient temperature sensor
 * - Optional circuit water return temperature sensor
 * - Automatic circuit turn-off based on outdoor temperature evolution
 * - Timed cooldown at turn-off
 * - Min/max limits on circuit water temperature
 * - Logging of state and temperatures
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * no concurrent operation is ever expected to happen to a given hcircuit, with the exception of
 * logging activity for which only data races are prevented via relaxed operations.
 * It is worth noting that no data consistency is guaranteed for logging, i.e. the data points logged
 * during a particular call of hcircuit_logdata_cb() may represent values from different time frames:
 * the overhead of ensuring consistency seems overkill for the purpose served by the log facility.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>
#include <string.h>	// memset

#include "pump.h"
#include "valve.h"
#include "models.h"
#include "hcircuit.h"
#include "io/inputs.h"
#include "lib.h"
#include "runtime.h"
#include "log/log.h"
#include "scheduler.h"
#include "storage.h"
#include "alarms.h"

#define HCIRCUIT_RORH_1HTAU	(3600*TIMEKEEP_SMULT)	///< 1h tau expressed in internal time representation
#define HCIRCUIT_RORH_DT	(10*TIMEKEEP_SMULT)	///< absolute min for 3600s tau is 8s dt, use 10s
#define HCIRCUIT_STORAGE_PREFIX	"hcircuit"

/**
 * Heating circuit data log callback.
 * @param ldata the log data to populate
 * @param object the opaque pointer to heating circuit structure
 * @return exec status
 */
static int hcircuit_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_hcircuit * const circuit = object;
	unsigned int i = 0;

	assert(ldata);
	assert(ldata->nkeys >= 7);

	if (!circuit)
		return (-EINVALID);

	if (!circuit->run.online)
		return (-EOFFLINE);

	ldata->values[i++] = aler(&circuit->run.runmode);
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.request_ambient));
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.target_ambient));
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.actual_ambient));
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.target_wtemp));
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.actual_wtemp));
	ldata->values[i++] = temp_to_int4log(aler(&circuit->run.heat_request));

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for a given circuit.
 * @param circuit the target circuit
 * @return (statically allocated) s_log_source pointer
 * @warning must not be called concurrently
 */
static const struct s_log_source * hcircuit_lsrc(const struct s_hcircuit * const circuit)
{
	static const log_key_t keys[] = {
		"runmode", "request_ambient", "target_ambient", "actual_ambient", "target_wtemp", "actual_wtemp", "heat_request",
	};
	static const enum e_log_metric metrics[] = {
		LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE,
	};
	const log_version_t version = 2;
	static struct s_log_source Hcircuit_lreg;

	Hcircuit_lreg = (struct s_log_source){
		.log_sched = LOG_SCHED_5mn,
		.basename = HCIRCUIT_STORAGE_PREFIX,
		.identifier = circuit->name,
		.version = version,
		.logdata_cb = hcircuit_logdata_cb,
		.nkeys = ARRAY_SIZE(keys),
		.keys = keys,
		.metrics = metrics,
		.object = circuit,
	};
	return (&Hcircuit_lreg);
}

/**
 * Register a circuit for logging.
 * @param circuit the target circuit
 * @return exec status
 */
static int hcircuit_log_register(const struct s_hcircuit * const circuit)
{
	assert(circuit);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->set.log)
		return (ALL_OK);

	return (log_register(hcircuit_lsrc(circuit)));
}

/**
 * Deregister a circuit from logging.
 * @param circuit the target circuit
 * @return exec status
 */
static int hcircuit_log_deregister(const struct s_hcircuit * const circuit)
{
	assert(circuit);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->set.log)
		return (ALL_OK);

	return (log_deregister(hcircuit_lsrc(circuit)));
}

/**
 * Bilinear water temperature law.
 * This law approximates the curvature resulting from limited transmission non-linearities in heating elements
 * by splitting the curve in two linear segments around an inflexion point. It works well for 1 < nH < 1.5.
 * The target output water temperature is computed for a 20°C target ambient. It is then shifted accordingly to
 * the actual target ambient temp, based on the original (linear) curve slope.
 * Most of these calculations are empirical "industry proven practices".
 *
 * - https://pompe-a-chaleur.ooreka.fr/astuce/voir/111578/le-regulateur-loi-d-eau-pour-pompe-a-chaleur
 * - http://www.energieplus-lesite.be/index.php?id=10959
 * - http://herve.silve.pagesperso-orange.fr/regul.htm
 *
 * @param circuit self
 * @param source_temp outdoor temperature to consider
 * @return a target water temperature for this circuit
 * @warning may overflow under adverse conditions
 */
static temp_t templaw_bilinear(const struct s_hcircuit * const circuit, const temp_t source_temp)
{
	const struct s_tlaw_bilin20C_priv * const tld = circuit->tlaw_priv;
	tempdiff_t t_output;
	tempdiff_t diffnum, diffden;
	tempdiff_t slopenum, slopeden;

	assert(tld);

	// hcircuit_make_bilinear() ensure tout1 > tout2 and twater1 < twater2 and (toufinfl < tout1) and (toutinfl > tout2)

	slopenum = (tempdiff_t)(tld->set.twater2 - tld->set.twater1);
	slopeden = (tempdiff_t)(tld->set.tout2 - tld->set.tout1);

	// calculate new parameters based on current outdoor temperature (select adequate segment)
	if (source_temp < tld->run.toutinfl) {
		diffnum = (tempdiff_t)(tld->run.twaterinfl - tld->set.twater1);
		diffden = (tempdiff_t)(tld->run.toutinfl - tld->set.tout1);
	}
	else {
		diffnum = (tempdiff_t)(tld->set.twater2 - tld->run.twaterinfl);
		diffden = (tempdiff_t)(tld->set.tout2 - tld->run.toutinfl);
	}

	// calculate output at nominal 20C: Y = input*slope + offset

	// XXX under "normal" conditions, the following operations should not overflow
	t_output = (tempdiff_t)(source_temp - tld->run.toutinfl) * diffnum;
	t_output /= diffden;		// no rounding: will slightly over estimate output, which is desirable
	t_output += tld->run.twaterinfl;

	// shift output based on actual target temperature: (tgt - 20C) * (1 - slope)
	t_output += (tempdiff_t)(aler(&circuit->run.target_ambient) - celsius_to_temp(20)) * (slopeden - slopenum) / slopeden;

	assert(!validate_temp((temp_t)t_output));

	return ((temp_t)t_output);
}

/**
 * Put circuit online.
 * Perform all necessary actions to prepare the circuit for service and
 * mark it as online.
 * @param circuit target circuit
 * @return exec status
 */
int hcircuit_online(struct s_hcircuit * const circuit)
{
	temp_t temp;
	int ret;

	if (!circuit)
		return (-EINVALID);

	assert(circuit->pdata);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->set.p.bmodel)
		return (-EMISCONFIGURED);

	// check that mandatory sensors are set
	ret = inputs_temperature_get(circuit->set.tid_outgoing, NULL);
	if (ret) {
		pr_err(_("\"%s\": tid_outgoing failed! (%d)"), circuit->name, ret);
		ret = - EMISCONFIGURED;
	}

	// limit_wtmax must be > 0C
	temp = SETorDEF(circuit->set.params.limit_wtmax, circuit->pdata->set.def_hcircuit.limit_wtmax);
	if (temp <= celsius_to_temp(0)) {
		pr_err(_("\"%s\": limit_wtmax must be locally or globally > 0°C"), circuit->name);
		ret = -EMISCONFIGURED;
	}

	// make sure associated building model is configured
	if (!circuit->set.p.bmodel || !circuit->set.p.bmodel->set.configured) {
		pr_err(_("\"%s\": building model \"%s\" is set but not configured"), circuit->name, circuit->set.p.bmodel->name);
		ret = -EMISCONFIGURED;
	}
	// if pump exists check it's correctly configured
	if (circuit->set.p.pump_feed && !circuit->set.p.pump_feed->set.configured) {
		pr_err(_("\"%s\": pump_feed \"%s\" is set but not configured"), circuit->name, circuit->set.p.pump_feed->name);
		ret = -EMISCONFIGURED;
	}

	// if mix valve exists check it's correctly configured
	if (circuit->set.p.valve_mix) {
		if (!circuit->set.p.valve_mix->set.configured) {
			pr_err(_("\"%s\": valve_mix \"%s\" is set but not configured"), circuit->name, circuit->set.p.valve_mix->name);
			ret = -EMISCONFIGURED;
		}
		else if (VA_TYPE_MIX != circuit->set.p.valve_mix->set.type) {
			pr_err(_("\"%s\": Invalid type for valve_mix \"%s\" (mixing valve expected)"), circuit->name, circuit->set.p.valve_mix->name);
			ret = -EMISCONFIGURED;
		}
	}

	if (circuit->set.wtemp_rorh) {
		// if ror is requested and valve is not available report misconfiguration
		if (!circuit->set.p.valve_mix) {
			pr_err(_("\"%s\": rate of rise control requested but no mixing valve is available"), circuit->name);
			ret = -EMISCONFIGURED;
		}
		// setup rate limiter
		circuit->run.rorh_temp_increment = temp_expw_mavg(0, circuit->set.wtemp_rorh, HCIRCUIT_RORH_1HTAU, HCIRCUIT_RORH_DT);
	}

	if (ALL_OK == ret) {
		circuit->run.online = true;

		// log registration shouldn't cause onlining to fail
		if (hcircuit_log_register(circuit) != ALL_OK)
			pr_err(_("\"%s\": couldn't register for logging"), circuit->name);
	}

	return (ret);
}

/**
 * Shutdown an online circuit.
 * Perform all necessary actions to completely shut down the circuit.
 * @param circuit target circuit
 * @return exec status
 */
static int hcircuit_shutdown(struct s_hcircuit * const circuit)
{
	assert(circuit);
	assert(circuit->set.configured);

	// XXX ensure actuators are reset after summer maintenance
	if (circuit->set.p.pump_feed)
		pump_shutdown(circuit->set.p.pump_feed);

	if (circuit->set.p.valve_mix)
		valve_shutdown(circuit->set.p.valve_mix);

	if (!circuit->run.active)
		return (ALL_OK);

	aser(&circuit->run.heat_request, RWCHCD_TEMP_NOREQUEST);
	aser(&circuit->run.target_wtemp, 0);
	circuit->run.rorh_update_time = 0;

	circuit->run.active = false;
	
	return (ALL_OK);
}

/**
 * Put circuit offline.
 * Perform all necessary actions to completely shut down the circuit and
 * mark is as offline.
 * @note will turn off logging for that circuit
 * @param circuit target circuit
 * @return error status
 */
int hcircuit_offline(struct s_hcircuit * const circuit)
{
	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	hcircuit_shutdown(circuit);
	hcircuit_log_deregister(circuit);

	memset(&circuit->run, 0x00, sizeof(circuit->run));
	//circuit->run.runmode = RM_OFF;// handled by memset
	//circuit->run.online = false;	// handled by memset

	return (ALL_OK);
}

/**
 * Conditions for running heating circuit.
 * The trigger temperature is the lowest of the set.outhoff_MODE and requested_ambient
 *
 * Circuit is off in @b ANY of the following conditions are met:
 * - building model summer is true
 * - t_out > current temp_trigger
 * - t_out_mix > current temp_trigger
 *
 * Circuit is back on if @b ALL of the following conditions are met:
 * - building model summer is false
 * - t_out < current temp_trigger - outhoff_hysteresis
 * - t_out_mix < current temp_trigger - outhoff_hysteresis
 *
 * State is preserved in all other cases.
 * Using t_out_mix instead of raw t_out_filt will make it possible to "weigh" the
 * influence of the building time constant per circuit (assuming a different t_out_mix ratio).
 * @param circuit the target circuit
 * @param runmode the target runmode for this circuit
 * @note This function needs run.request_ambient to be set prior calling for optimal operation
 */
static void hcircuit_outhoff(struct s_hcircuit * const circuit, const enum e_runmode runmode)
{
	const struct s_bmodel * restrict const bmodel = circuit->set.p.bmodel;
	temp_t temp_trigger, temp_request, t_out, t_out_mix;

	// input sanitization performed in logic_hcircuit()
	assert(circuit->pdata);
	assert(bmodel);

	// check for summer switch off first
	if (aler(&bmodel->run.summer)) {
		circuit->run.outhoff = true;
		return;
	}

	switch (runmode) {
		case RM_COMFORT:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_comfort, circuit->pdata->set.def_hcircuit.outhoff_comfort);
			break;
		case RM_ECO:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_eco, circuit->pdata->set.def_hcircuit.outhoff_eco);
			break;
		case RM_DHWONLY:
		case RM_FROSTFREE:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_frostfree, circuit->pdata->set.def_hcircuit.outhoff_frostfree);
			break;
		case RM_OFF:
		case RM_AUTO:
		case RM_TEST:
		case RM_UNKNOWN:
		default:
			return;
	}

	// min of setting and current ambient request
	temp_request = aler(&circuit->run.request_ambient);
	temp_trigger = (temp_request < temp_trigger) ? temp_request : temp_trigger;

	if (!temp_trigger) {	// don't do anything if we have an invalid limit
		circuit->run.outhoff = false;
		return;
	}

	t_out = aler(&bmodel->run.t_out);
	t_out_mix = aler(&bmodel->run.t_out_mix);

	if ((t_out > temp_trigger) ||
	    (t_out_mix > temp_trigger)) {
		circuit->run.outhoff = true;
	}
	else {
		temp_trigger -= SETorDEF(circuit->set.params.outhoff_hysteresis, circuit->pdata->set.def_hcircuit.outhoff_hysteresis);
		if ((t_out < temp_trigger) &&
		    (t_out_mix < temp_trigger))
			circuit->run.outhoff = false;
	}
}

/**
 * Heating circuit logic.
 * Sets the target ambient temperature for a circuit based on selected run mode.
 * Runs the ambient model, and applies temperature shift based on mesured or
 * modelled ambient temperature. Handles runmode transitions.
 * @param circuit target circuit
 * @return exec status
 * @note the ambient model has a hackish acknowledgment of lag due to circuit warming up
 * @note during TRANS_UP the boost transition timer will be reset when a runmode change results in
 * TRANS_UP remaining active, i.e. the boost can be applied for a total time longer than the set time.
 * @note this function performs some checks to work around uninitialized data at startup, maybe this should be handled in online() instead.
 * @todo add optimizations (anticipated turn on/off, max ambient...)
 * @todo ambient max delta shutdown; optim based on return temp
 */
__attribute__((warn_unused_result))
int hcircuit_logic(struct s_hcircuit * restrict const circuit)
{
	const struct s_schedule_eparams * eparams;
	const struct s_bmodel * restrict bmodel;
	enum e_runmode prev_runmode, new_runmode;
	temp_t request_temp, target_ambient, ambient_temp;
	timekeep_t elapsed_time, dtmin;
	const timekeep_t now = timekeep_now();
	bool can_fastcool;

	assert(circuit);

	bmodel = circuit->set.p.bmodel;
	assert(bmodel);

	// fast cooldown can only be applied if set AND not in frost condition
	can_fastcool = (circuit->set.fast_cooldown && !aler(&bmodel->run.frost));

	// store current status for transition detection
	prev_runmode = aler(&circuit->run.runmode);

	// handle global/local runmodes
	if (RM_AUTO == circuit->set.runmode) {
		// if we have a schedule, use it, or global settings if unavailable
		eparams = scheduler_get_schedparams(circuit->set.schedid);
		new_runmode = ((SYS_AUTO == runtime_systemmode()) && eparams) ? eparams->runmode : runtime_runmode();
	}
	else
		new_runmode = circuit->set.runmode;

	// if an absolute priority DHW charge is in progress, switch to dhw-only (will register the transition)
	if (circuit->pdata->run.dhwc_absolute)
		new_runmode = RM_DHWONLY;

	// depending on circuit run mode, assess circuit target temp
	switch (new_runmode) {
		case RM_OFF:
		case RM_TEST:
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			request_temp = SETorDEF(circuit->set.params.t_comfort, circuit->pdata->set.def_hcircuit.t_comfort);
			break;
		case RM_ECO:
			request_temp = SETorDEF(circuit->set.params.t_eco, circuit->pdata->set.def_hcircuit.t_eco);
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			dbgerr("\"%s\": invalid runmode (%d), falling back to RM_FROSTREE", circuit->name, new_runmode);
			new_runmode = RM_FROSTFREE;
			// fallthrough
		case RM_DHWONLY:
		case RM_FROSTFREE:
			request_temp = SETorDEF(circuit->set.params.t_frostfree, circuit->pdata->set.def_hcircuit.t_frostfree);
			break;
	}

	// apply offset
	request_temp += SETorDEF(circuit->set.params.t_offset, circuit->pdata->set.def_hcircuit.t_offset);
	target_ambient = request_temp;

	// save current ambient request (needed by hcircuit_outhoff())
	aser(&circuit->run.request_ambient, request_temp);

	// Check if the circuit meets run.outhoff conditions
	hcircuit_outhoff(circuit, new_runmode);
	// if the circuit does meet the conditions (and frost is not in effect), turn it off: update runmode.
	if (circuit->run.outhoff && !aler(&bmodel->run.frost))
		new_runmode = RM_OFF;

	// transition detection - check actual_ambient to avoid false trigger at e.g. startup
	ambient_temp = aler(&circuit->run.actual_ambient);
	if ((prev_runmode != new_runmode) && ambient_temp) {
		circuit->run.transition = (ambient_temp > request_temp) ? TRANS_DOWN : TRANS_UP;
		circuit->run.trans_start_temp = ambient_temp;
		circuit->run.trans_active_elapsed = 0;
		circuit->run.ambient_update_time = now;	// reset timer
	}

	elapsed_time = now - circuit->run.ambient_update_time;

	// handle extra transition logic
	switch (circuit->run.transition) {
		case TRANS_DOWN:
			circuit->run.trans_active_elapsed += elapsed_time;
			// floor output during down transition if requested by the plant, except when absolute DHWT priority charge is in effect
			if (circuit->pdata->run.consumer_sdelay && !circuit->pdata->run.dhwc_absolute)
				circuit->run.floor_output = true;
			// if fast cooldown is possible, turn off circuit
			if (!circuit->run.floor_output && can_fastcool)
				new_runmode = RM_OFF;
			break;
		case TRANS_UP:
			circuit->run.trans_active_elapsed += elapsed_time;
			// apply boost target
			if (circuit->run.trans_active_elapsed < circuit->set.boost_maxtime)
				target_ambient += circuit->set.tambient_boostdelta;
			break;
		case TRANS_NONE:
		default:
			break;
	}

	aser(&circuit->run.runmode, new_runmode);

	// reset output flooring ONLY when sdelay is elapsed (avoid early reset if transition ends early)
	if (!circuit->pdata->run.consumer_sdelay)
		circuit->run.floor_output = false;

	// XXX OPTIM if return temp is known

	// Ambient temperature is either read or modelled
	if (inputs_temperature_get(circuit->set.tid_ambient, &ambient_temp) == ALL_OK) {	// we have an ambient sensor
												// calculate ambient shift based on measured ambient temp influence in percent
		target_ambient += (circuit->set.ambient_factor) * (tempdiff_t)(target_ambient - ambient_temp) / 100;
		circuit->run.ambient_update_time = now;
	}
	else {	// no sensor (or faulty), apply ambient model
		dtmin = expw_mavg_dtmin(3*bmodel->set.tau);

		// if circuit is OFF (due to outhoff()) apply moving average based on outdoor temp
		if (RM_OFF == new_runmode) {
			if (unlikely(!ambient_temp))	// startup in RM_OFF
				ambient_temp = aler(&bmodel->run.t_out_mix);
			else if (elapsed_time > dtmin) {
				ambient_temp = temp_expw_mavg(ambient_temp, aler(&bmodel->run.t_out_mix), 3*bmodel->set.tau, elapsed_time); // we converge toward low_temp
				circuit->run.ambient_update_time = now;
			}
			dbgmsg(1, 1, "\"%s\": off, ambient: %.1f", circuit->name, temp_to_celsius(ambient_temp));
		}
		else {
			// otherwise apply transition models. Circuit cannot be RM_OFF here
			switch (circuit->run.transition) {
				case TRANS_UP:
					//  model up temp only if hcircuit wtempt is at least within 5K of target
					if (aler(&circuit->run.actual_wtemp) <= (aler(&circuit->run.target_wtemp) - deltaK_to_temp(5))) {
						circuit->run.ambient_update_time = now;
						break;
					}
					// fallthrough - same computation applied on up and down
				case TRANS_DOWN:
					// apply logarithmic model
					if (elapsed_time > dtmin) {
						circuit->run.ambient_update_time = now;
						// converge over bmodel tau
						ambient_temp = temp_expw_mavg(ambient_temp, target_ambient, bmodel->set.tau, elapsed_time);
					}
					break;
				default:
				case TRANS_NONE:
					// no transition, ambient temp assumed to be request temp
					ambient_temp = request_temp;
					circuit->run.ambient_update_time = now;
					break;
			}
		}
	}

	// store current ambient & target temp
	aser(&circuit->run.actual_ambient, ambient_temp);
	aser(&circuit->run.target_ambient, target_ambient);

	// handle transitions - transition is over when we are 1K from target
	switch (circuit->run.transition) {
		case TRANS_DOWN:
			if (ambient_temp <= (request_temp + deltaK_to_temp(1)))
				circuit->run.transition = TRANS_NONE;	// transition completed
			break;
		case TRANS_UP:
			if (ambient_temp >= (request_temp - deltaK_to_temp(1)))
				circuit->run.transition = TRANS_NONE;	// transition completed
			break;
		case TRANS_NONE:
		default:
			break;
	}

	dbgmsg(1, (circuit->run.transition), "\"%s\": Trans: %d, st_amb: %.1f, cr_amb: %.1f, active_elapsed: %u",
	       circuit->name, circuit->run.transition, temp_to_celsius(circuit->run.trans_start_temp), temp_to_celsius(ambient_temp), timekeep_tk_to_sec(circuit->run.trans_active_elapsed));

	return (ALL_OK);
}

/**
 * Rate-of-rise limiter.
 * @param circuit the target circuit
 * @param curr_temp the current circuit output water temperature
 * @param target_temp the current circuit target water temperature
 * @return the RoR-limited value for target temperature
 */
static temp_t hcircuit_ror_limiter(struct s_hcircuit * restrict const circuit, const temp_t curr_temp, temp_t target_temp)
{
	const timekeep_t now = timekeep_now();
	temp_t temp;

	dbgmsg(2, 1, "\"%s\": ror last_tg: %.1f", circuit->name, temp_to_celsius(circuit->run.rorh_last_target));
	// first sample: init target to current temp and set water_temp to current
	if (!circuit->run.rorh_update_time) {
		target_temp = curr_temp;
		circuit->run.rorh_last_target = curr_temp;	// update last_target to current point
		circuit->run.rorh_update_time = now + timekeep_sec_to_tk(60);	// send update_time 60s ahead for low point settling (see below). XXX hardcoded
	}
	// at circuit startup (pump was previously off) let the water settle to lowest point, which we'll use as reference once it's reached.
	else if (timekeep_a_ge_b(circuit->run.rorh_update_time, now)) {
		target_temp = curr_temp;
		if (curr_temp < circuit->run.rorh_last_target)
			circuit->run.rorh_last_target = curr_temp;
		// if the heat source has not yet reached optimal output, wait before resuming normal algorithm operation
		if (circuit->pdata->run.consumer_shift < 0)
			circuit->run.rorh_update_time = now + timekeep_sec_to_tk(30);
	}
	// startup is done.
	// Request for temp lower than (or equal) current: don't touch water_temp (let low request pass), update target to current
	else if (target_temp <= curr_temp) {
		circuit->run.rorh_last_target = curr_temp;	// update last_target to current point
		circuit->run.rorh_update_time = now;
	}
	// else: request for higher temp: apply rate limiter: target_temp is updated every minute
	else {
		if ((now - circuit->run.rorh_update_time) >= HCIRCUIT_RORH_DT) {
			// compute next target step
			temp = circuit->run.rorh_last_target + circuit->run.rorh_temp_increment;
			// new request is min of next target step and actual request
			circuit->run.rorh_last_target = (temp < target_temp) ? temp : target_temp;
			circuit->run.rorh_update_time = now;
		}
		target_temp = circuit->run.rorh_last_target;	// apply current step
	}

	return (target_temp);
}

/**
 * Circuit failsafe routine.
 * By default we shutdown the circuit:
 * - remove heat request
 * - close the valve (if any)
 * - start the pump (if any)
 *
 * The logic being that we cannot make any assumption as to whether or not it is
 * safe to open the valve, whereas closing it will always be safe.
 * Turning on the pump mitigates frost risks.
 * @param circuit target circuit
 */
static void hcircuit_failsafe(struct s_hcircuit * restrict const circuit)
{
	assert(circuit);
	aser(&circuit->run.heat_request, RWCHCD_TEMP_NOREQUEST);
	valve_reqclose_full(circuit->set.p.valve_mix);
	if (circuit->set.p.pump_feed)
		(void)!pump_set_state(circuit->set.p.pump_feed, ON, FORCE);
}

/**
 * Circuit control loop.
 * Controls the circuits elements to achieve the desired target temperature.
 * @param circuit target circuit
 * @return exec status
 * @warning circuit->run.target_ambient must be properly set before this runs
 * @note this function ensures that in the event of an error, the hcircuit is put in a failsafe state as defined in hcircuit_failsafe().
 */
int hcircuit_run(struct s_hcircuit * const circuit)
{
	temp_t water_temp, curr_temp, ret_temp, lwtmin, lwtmax;
	int ret;

	if (unlikely(!circuit))
		return (-EINVALID);

	if (unlikely(!circuit->run.online))	// implies set.configured == true
		return (-EOFFLINE);

	// safety checks
	ret = inputs_temperature_get(circuit->set.tid_outgoing, &curr_temp);
	if (unlikely(ALL_OK != ret)) {
		alarms_raise(ret, _("HCircuit \"%s\": failed to get outgoing temp!"), circuit->name);
		goto fail;
	}

	// we're good to go - keep updating actual_wtemp when circuit is off
	aser(&circuit->run.actual_wtemp, curr_temp);

	ret = hcircuit_logic(circuit);
	if (unlikely(ALL_OK != ret))
		goto fail;

	// force circuit ON during hs_overtemp condition
	if (unlikely(circuit->pdata->run.hs_overtemp))
		aser(&circuit->run.runmode, RM_COMFORT);

	// handle special runmode cases
	switch (aler(&circuit->run.runmode)) {
		case RM_OFF:
			if (aler(&circuit->run.target_wtemp) && (circuit->pdata->run.consumer_sdelay > 0)) {
				// disable heat request from this circuit
				aser(&circuit->run.heat_request, RWCHCD_TEMP_NOREQUEST);
				dbgmsg(2, 1, "\"%s\": in cooldown, remaining: %u", circuit->name, timekeep_tk_to_sec(circuit->pdata->run.consumer_sdelay));
				return (ALL_OK);	// stop processing: maintain current output
			}
			else
				return (hcircuit_shutdown(circuit));
		case RM_TEST:
			circuit->run.active = true;
			valve_reqstop(circuit->set.p.valve_mix);
			if (circuit->set.p.pump_feed)
				(void)!pump_set_state(circuit->set.p.pump_feed, ON, FORCE);
			return (ALL_OK);
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			ret = -EINVALIDMODE;	// this can never happen due to fallback in _logic()
			goto fail;
	}

	// if we reached this point then the circuit is active
	circuit->run.active = true;

	// circuit is active, ensure pump is running
	if (circuit->set.p.pump_feed) {
		ret = pump_set_state(circuit->set.p.pump_feed, ON, 0);
		if (unlikely(ALL_OK != ret)) {
			alarms_raise(ret, _("HCircuit \"%s\": failed to request feed pump \"%s\" ON"), circuit->name, circuit->set.p.pump_feed->name);
			goto fail;
		}
	}

	// fetch limits
	lwtmin = SETorDEF(circuit->set.params.limit_wtmin, circuit->pdata->set.def_hcircuit.limit_wtmin);
	lwtmax = SETorDEF(circuit->set.params.limit_wtmax, circuit->pdata->set.def_hcircuit.limit_wtmax);

	// calculate water pipe temp
	switch (circuit->set.tlaw) {
		case HCL_BILINEAR:
			water_temp = templaw_bilinear(circuit, aler(&circuit->set.p.bmodel->run.t_out_mix));
			break;
		case HCL_NONE:
		default:
			water_temp = RWCHCD_TEMP_NOREQUEST;	// can never happen, enforced by online()
			break;
	}

	// enforce limits

	if (water_temp < lwtmin)
		water_temp = lwtmin;
	else if (water_temp > lwtmax)
		water_temp = lwtmax;

	// save "non-interfered" target water temp, i.e. the real target (within enforced limits)
	aser(&circuit->run.target_wtemp, water_temp);

	// heat request is always computed based on non-interfered water_temp value
	aser(&circuit->run.heat_request, water_temp + SETorDEF(circuit->set.params.temp_inoffset, circuit->pdata->set.def_hcircuit.temp_inoffset));

	// alterations to the computed value only make sense if a mixing valve is available
	if (circuit->set.p.valve_mix) {
		// interference: apply rate of rise limitation if any
		// applied first so it's not impacted by the next interferences (in particular power shift). XXX REVIEW: might be needed to move after if ror control is desired on cshift rising edges
		if (circuit->set.wtemp_rorh)
			water_temp = hcircuit_ror_limiter(circuit, curr_temp, water_temp);

		// interference: handle output flooring requests: maintain current or higher wtemp
		if (circuit->run.floor_output)
			water_temp = (water_temp > curr_temp) ? water_temp : curr_temp;

		// interference: apply global power shift
		if (circuit->pdata->run.consumer_shift) {
			ret = inputs_temperature_get(circuit->set.tid_return, &ret_temp);
			// if we don't have a return temp or if the return temp is higher than the outgoing temp, use 0°C (absolute physical minimum) as reference
			if ((ALL_OK != ret) || (ret_temp >= water_temp))
				ret_temp = celsius_to_temp(0);

			// X% shift is (current + X*(current - ref)/100). ref is return temp
			water_temp += circuit->pdata->run.consumer_shift * (tempdiff_t)(water_temp - ret_temp) / 100;
		}

		// enforce maximum temp during overtemp condition
		if (circuit->pdata->run.hs_overtemp)
			water_temp = lwtmax;

		// low limit can be overriden by external interferences
		// but high limit can never be overriden: re-enact it
		if (water_temp > lwtmax)
			water_temp = lwtmax;

		// adjust valve position if necessary
		ret = valve_mix_tcontrol(circuit->set.p.valve_mix, water_temp);
		if (unlikely(ret)) {
			alarms_raise(ret, _("HCircuit \"%s\": failed to control mixing valve \"%s\""), circuit->name, circuit->set.p.valve_mix->name);
			goto fail;
		}
	}

#ifdef DEBUG
	(void)!inputs_temperature_get(circuit->set.tid_return, &ret_temp);
	dbgmsg(1, 1, "\"%s\": rq_amb: %.1f, tg_amb: %.1f, amb: %.1f, tg_wt: %.1f, tg_wt_mod: %.1f, cr_wt: %.1f, cr_rwt: %.1f", circuit->name,
	       temp_to_celsius(aler(&circuit->run.request_ambient)), temp_to_celsius(aler(&circuit->run.target_ambient)), temp_to_celsius(aler(&circuit->run.actual_ambient)),
	       temp_to_celsius(aler(&circuit->run.target_wtemp)), temp_to_celsius(water_temp), temp_to_celsius(curr_temp), temp_to_celsius(ret_temp));
#endif

	return (ALL_OK);

fail:
	hcircuit_failsafe(circuit);
	return (ret);
}

/**
 * Assign bilinear temperature law to the circuit.
 * This function is used to assign or update a bilinear temperature law (and its
 * associated parameters) to a target circuit.
 * To determine the position of the inflexion point, the calculation starts from the linear curve as determined
 * by the two set points. It then computes the outdoor temperature corresponding to a 20°C water output temp.
 * Then, it computes the temperature differential between the lowest outdoor temp set point and that calculated value.
 * The inflexion point is located on that differential, 30% down from the 20°C output water temp point.
 * Thus, the high outdoor temp set point does NOT directly determines the position of the inflexion point.
 *
 * @param circuit target circuit
 * @param tout1 outside (low) temperature for point 1
 * @param twater1 water (high) temperature for point 1
 * @param tout2 outside (high) temperature for point 2
 * @param twater2 water (low) temperature for point 2
 * @param nH100 thermal non-linearity coef *100
 * @return error status
 */
int hcircuit_make_bilinear(struct s_hcircuit * const circuit,
			  temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, uint_least16_t nH100)
{
	struct s_tlaw_bilin20C_priv * priv = NULL;
	temp_t toutw20C, tlin, offset;
	tempdiff_t diffnum, diffden;
	float slope, tfl;

	if (!circuit)
		return (-EINVALID);

	// validate input
	if ((tout1 >= tout2) || (twater1 <= twater2))
		return (-EINVALID);

	if (tout1 >= celsius_to_temp(20))
		return (-EINVALID);

	// create priv element if it doesn't already exist
	if (!circuit->tlaw_priv) {
		priv = calloc(1, sizeof(*priv));
		if (!priv)
			return (-EOOM);
	}
	else if ((HCL_BILINEAR == circuit->set.tlaw) && circuit->tlaw_priv)
		priv = circuit->tlaw_priv;
	else
		return (-EINVALID);

	priv->set.tout1 = tout1;
	priv->set.twater1 = twater1;
	priv->set.tout2 = tout2;
	priv->set.twater2 = twater2;
	priv->set.nH100 = nH100;

	// calculate the linear slope = (Y2 - Y1)/(X2 - X1)
	diffnum = (tempdiff_t)(twater2 - twater1);
	diffden = (tempdiff_t)(tout2 - tout1);
	slope = (float)diffnum / (float)diffden;
	// offset: reduce through a known point
	tfl = (float)tout2 * slope;
	// XXX assert tfl can be represented as tempdiff_t, which it should, by definition
	offset = (twater2 - (tempdiff_t)(tfl));

	// calculate outdoor temp for 20C water temp
	diffnum = (tempdiff_t)(celsius_to_temp(20) - offset);
	tfl = (float)diffnum / slope;
	// XXX assert result can be represented as temp_t, which it should by definition
	toutw20C = (temp_t)tfl;

	// calculate outdoor temp for inflexion point (toutw20C - (30% of toutw20C - tout1))
	priv->run.toutinfl = toutw20C - ((toutw20C - tout1) * 30 / 100);

	// calculate corrected water temp at inflexion point (tlinear[nH=1] - 20C) * (nH - 1)
	tfl = (float)priv->run.toutinfl * slope;
	tlin = (tempdiff_t)tfl + offset;
	priv->run.twaterinfl = tlin + ((tlin - celsius_to_temp(20)) * (nH100 - 100) / 100);

	if ((priv->run.toutinfl <= tout1) || (priv->run.toutinfl >= tout2) || (priv->run.twaterinfl > twater1) || (priv->run.twaterinfl < twater2)) {
		dbgerr("\"%s\": bilinear inflexion point computation failed!", circuit->name);
		free(priv);
		return (-EINVALID);
	}

	// attach priv structure
	circuit->tlaw_priv = priv;

	circuit->set.tlaw = HCL_BILINEAR;

	return (ALL_OK);
}

/**
 * Circuit destructor.
 * Frees all circuit-local resources
 * @param circuit the circuit to delete
 */
void hcircuit_cleanup(struct s_hcircuit * circuit)
{
	if (!circuit)
		return;

	free((void *)circuit->name);
	circuit->name = NULL;
	free(circuit->tlaw_priv);
	circuit->tlaw_priv = NULL;
}
