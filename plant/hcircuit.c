//
//  plant/hcircuit.c
//  rwchcd
//
//  (C) 2017-2022 Thibaut VARENE
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
 * - Accelerated cooldown (per-runmode) and boost warmup transitions
 * - Optional circuit ambient temperature sensor
 * - Optional circuit water return temperature sensor
 * - Automatic circuit turn-off based on indoor/outdoor temperature evolution
 * - Timed cooldown at turn-off
 * - Min/max limits on circuit water temperature
 * - Logging of state and temperatures
 * - summer maintenance of actuators when operating in frostfree/dhwonly modes
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * no concurrent operation is ever expected to happen to a given hcircuit, with the exception of
 * logging activity for which only data races are prevented via relaxed operations.
 * It is worth noting that no data consistency is guaranteed for logging, i.e. the data points logged
 * during a particular call of hcircuit_logdata_cb() may represent values from different time frames:
 * the overhead of ensuring consistency seems overkill for the purpose served by the log facility.
 *
 * @note in "test" mode the mixing valve (if any) is stopped (so that it can be manually adjusted
 * as needed). During summer maintenance it is opened in full.
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
#include "hcircuit_priv.h"

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

	if (!aler(&circuit->run.online))
		return (-EOFFLINE);

	ldata->values[i++].i = aler(&circuit->run.runmode);
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.request_ambient));
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.target_ambient));
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.actual_ambient));
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.target_wtemp));
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.actual_wtemp));
	ldata->values[i++].f = temp_to_celsius(aler(&circuit->run.heat_request));

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
		LOG_METRIC_IGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE, LOG_METRIC_FGAUGE,
	};
	const log_version_t version = 2;
	static struct s_log_source Hcircuit_lreg;

	Hcircuit_lreg = (struct s_log_source){
		.log_sched = LOG_SCHED_1mn,
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
	t_output += (tempdiff_t)tld->run.twaterinfl;

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
	// if pump exists check it's available
	if (circuit->set.p.pump_feed && !pump_is_online(circuit->set.p.pump_feed)) {
		pr_err(_("\"%s\": pump_feed \"%s\" is set but not online"), circuit->name, pump_name(circuit->set.p.pump_feed));
		ret = -EMISCONFIGURED;
	}

	// if mix valve exists check it's correctly configured
	if (circuit->set.p.valve_mix) {
		if (!valve_is_online(circuit->set.p.valve_mix)) {
			pr_err(_("\"%s\": valve_mix \"%s\" is set but not configured"), circuit->name, valve_name(circuit->set.p.valve_mix));
			ret = -EMISCONFIGURED;
		}
		else if (VA_TYPE_MIX != valve_get_type(circuit->set.p.valve_mix)) {
			pr_err(_("\"%s\": Invalid type for valve_mix \"%s\" (mixing valve expected)"), circuit->name, valve_name(circuit->set.p.valve_mix));
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

	// warn on unenforceable configuration
	if (circuit->set.params.inoff_temp) {
		if (inputs_temperature_get(circuit->set.tid_ambient, NULL) != ALL_OK)
			pr_warn(_("\"%s\": inoff_temp set but no ambient sensor available: ignored."), circuit->name);
	}

	if (ALL_OK == ret) {
		aser(&circuit->run.online, true);

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

	if (!circuit->run.active)
		return (ALL_OK);

	if (circuit->set.p.pump_feed)
		pump_shutdown(circuit->set.p.pump_feed);

	if (circuit->set.p.valve_mix)
		valve_shutdown(circuit->set.p.valve_mix);

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
 * Outdoor conditions for running heating circuit.
 * The trigger temperature is the lowest of the set.outhoff_MODE and requested_ambient
 *
 * Circuit is off if @b ANY of the following conditions are met:
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
		case RM_SUMMAINT:
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
 * Indoor conditions for running heating circuit.
 * Only applies when an ambient sensor is available and inoff_temp is set.
 *
 * Circuit is off if ambient temperature is > inoff_temp
 * Circuit is back on if ambient temperature is < inoff_temp - 1K; or ambient sensor is unavailable
 *
 * State is preserved in all other cases.
 * @param circuit the target circuit
 */
static void hcircuit_inoff(struct s_hcircuit * const circuit)
{
	temp_t temp_trigger, t_ambient;

	// input sanitization performed in logic_hcircuit()
	assert(circuit->pdata);

	temp_trigger = SETorDEF(circuit->set.params.inoff_temp, circuit->pdata->set.def_hcircuit.inoff_temp);
	if (!temp_trigger) {
		circuit->run.inoff = false;
		return;
	}

	if (inputs_temperature_get(circuit->set.tid_ambient, &t_ambient) == ALL_OK) {
		if (t_ambient > temp_trigger)
			circuit->run.inoff = true;
		else if (t_ambient < temp_trigger - deltaK_to_temp(1))
			circuit->run.inoff = false;
	}
	else
		circuit->run.inoff = false;
}

/**
 * Heating circuit logic.
 * Sets the target ambient temperature for a circuit based on selected run mode.
 * Runs the ambient model, and applies temperature shift based on mesured or
 * modelled ambient temperature. Handles runmode transitions.
 * Transitions are ended when temperature is within a set threshold of the target temp:
 * - 0.5°K when an indoor sensor is available
 * - 1°K otherwise
 * @param circuit target circuit
 * @return exec status
 * @note the ambient model has a hackish acknowledgment of lag due to circuit warming up
 * @note during TRANS_UP the boost transition timer will be reset when a runmode change results in
 * TRANS_UP remaining active, i.e. the boost can be applied for a total time longer than the set time.
 * @note this function performs some checks to work around uninitialized data at startup, maybe this should be handled in online() instead.
 * @todo add optimizations (anticipated turn on/off, max ambient...)
 * @todo optim based on return temp
 */
__attribute__((warn_unused_result))
int hcircuit_logic(struct s_hcircuit * restrict const circuit)
{
	const struct s_schedule_eparams * eparams;
	const struct s_bmodel * restrict bmodel;
	const enum e_systemmode sysmode = runtime_systemmode();
	enum e_runmode prev_runmode, new_runmode;
	temp_t request_temp, target_ambient, ambient_temp, trans_thrsh;
	timekeep_t elapsed_time, dtmin;
	const timekeep_t now = timekeep_now();
	bool can_fastcool, fastcool_mode = false;

	assert(circuit);

	bmodel = circuit->set.p.bmodel;
	assert(bmodel);

	// store current status for transition detection
	prev_runmode = aler(&circuit->run.runmode);

	// SYS_TEST/SYS_OFF always overrides
	if ((SYS_TEST == sysmode) || (SYS_OFF == sysmode))
		new_runmode = runtime_runmode();
	else {
		// handle global/local runmodes
		new_runmode = aler(&circuit->overrides.o_runmode) ? aler(&circuit->overrides.runmode) : circuit->set.runmode;
		if (RM_AUTO == new_runmode) {
			// if we have a schedule, use it, or global settings if unavailable
			eparams = scheduler_get_schedparams(circuit->set.schedid);
			new_runmode = ((SYS_AUTO == sysmode) && eparams) ? eparams->runmode : runtime_runmode();
		}
	}

	// if an absolute priority DHW charge is in progress, switch to dhw-only (will register the transition)
	if (circuit->pdata->run.dhwc_absolute)
		new_runmode = RM_DHWONLY;

	// if summer_maint is on, by definition the hcircuit has been and still is inactive, regardless of actual runmode
	if (circuit->pdata->run.summer_maint) {
		aser(&circuit->run.runmode, RM_SUMMAINT);
		return (ALL_OK);	// bypass everything
	}

	// depending on circuit run mode, assess circuit target temp
	switch (new_runmode) {
		case RM_OFF:
		case RM_TEST:
			aser(&circuit->run.runmode, new_runmode);
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			request_temp = SETorDEF(circuit->set.params.t_comfort, circuit->pdata->set.def_hcircuit.t_comfort);
			break;
		case RM_ECO:
			fastcool_mode = (circuit->set.fast_cooldown & FCM_ECO);
			request_temp = SETorDEF(circuit->set.params.t_eco, circuit->pdata->set.def_hcircuit.t_eco);
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		case RM_SUMMAINT:
		default:
			dbgerr("\"%s\": invalid runmode (%d), falling back to RM_FROSTREE", circuit->name, new_runmode);
			new_runmode = RM_FROSTFREE;
			// fallthrough
		case RM_DHWONLY:
		case RM_FROSTFREE:
			fastcool_mode = (circuit->set.fast_cooldown & FCM_FROSTFREE);
			request_temp = SETorDEF(circuit->set.params.t_frostfree, circuit->pdata->set.def_hcircuit.t_frostfree);
			break;
	}

	// fast cooldown can only be applied if set AND not in frost condition
	can_fastcool = (fastcool_mode && !aler(&bmodel->run.frost));

	// apply offsets
	request_temp += SETorDEF(circuit->set.params.t_offset, circuit->pdata->set.def_hcircuit.t_offset);
	request_temp += aler(&circuit->overrides.t_offset);
	target_ambient = request_temp;

	// save current ambient request (needed by hcircuit_outhoff())
	aser(&circuit->run.request_ambient, request_temp);

	// Check if the circuit meets outoff/inoff conditions
	hcircuit_outhoff(circuit, new_runmode);
	hcircuit_inoff(circuit);
	// if the circuit does meet the conditions (and frost is not in effect), turn it off: update runmode.
	if ((circuit->run.outhoff || circuit->run.inoff) && !aler(&bmodel->run.frost))
		new_runmode = RM_OFF;

	// Ambient temperature is either read or modelled
	ambient_temp = aler(&circuit->run.actual_ambient);
	if (inputs_temperature_get(circuit->set.tid_ambient, &ambient_temp) == ALL_OK) {	// we have an ambient sensor
												// calculate ambient shift based on measured ambient temp influence in percent
		target_ambient += circuit->set.ambient_factor * (tempdiff_t)(target_ambient - ambient_temp) / 100;
		circuit->run.ambient_update_time = now;
		trans_thrsh = deltaK_to_temp(0.5);	// apply a tight threshold for end-of-transition
	}
	else {	// no sensor (or faulty), apply ambient model
		elapsed_time = now - circuit->run.ambient_update_time;
		dtmin = expw_mavg_dtmin(3*bmodel->set.tau);
		trans_thrsh = deltaK_to_temp(1);

		if (unlikely(!ambient_temp))	// startup: ambient = outdoor in RM_OFF, request otherwise
			ambient_temp = (RM_OFF == new_runmode) ? aler(&bmodel->run.t_out_mix) : request_temp;

		// if circuit is OFF (due to outhoff()) apply moving average based on outdoor temp
		if (RM_OFF == prev_runmode) {	// use prev_runmode to capture TRANS_DOWN && can_fastcool - this delays "correct" computation by one cycle
			if (elapsed_time > dtmin) {
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
				case TRANS_NONE:
					// apply logarithmic model
					if (elapsed_time > dtmin) {
						circuit->run.ambient_update_time = now;
						// converge over bmodel tau
						ambient_temp = temp_expw_mavg(ambient_temp, target_ambient, bmodel->set.tau, elapsed_time);
					}
					break;
				default:
					break;
			}
		}
	}

	// transition detection
	if (prev_runmode != new_runmode) {
		circuit->run.transition = (ambient_temp > request_temp) ? TRANS_DOWN : TRANS_UP;
		circuit->run.trans_start_time = now;

		// request output flooring once when transitioning to lower power modes when no absolute DHWT priority charge is in effect
		if (!circuit->pdata->run.dhwc_absolute && lib_runmode_is_changedown(prev_runmode, new_runmode))
			circuit->run.floor_output = true;
	}

	// handle transitions logic - transition is over when we are trans_thrsh from target
	switch (circuit->run.transition) {
		case TRANS_DOWN:
			if (ambient_temp <= (request_temp + trans_thrsh))
				circuit->run.transition = TRANS_NONE;	// transition completed
			else if (can_fastcool && !circuit->run.floor_output)
				new_runmode = RM_OFF;	// enact RM_OFF on transition when possible (do it here to catch e.g. outoff deasserted but ambient temp warrants fastcool)
			break;
		case TRANS_UP:
			if (ambient_temp >= (request_temp - trans_thrsh))
				circuit->run.transition = TRANS_NONE;	// transition completed
			else {
				//  shift start time to delay stop trigger if hcircuit wtempt is not at least within 5K of target
				if (aler(&circuit->run.actual_wtemp) < (aler(&circuit->run.target_wtemp) - deltaK_to_temp(5)))
					circuit->run.trans_start_time += runtime_get_timestep();
				// apply boost target
				if ((now - circuit->run.trans_start_time) < circuit->set.boost_maxtime)
					target_ambient += circuit->set.tambient_boostdelta;
			}

			// detect end of boost for flooring in all cases (timeout or transition over)
			if (circuit->set.boost_maxtime) {
				// assume that a 1K+ downstep signals end of boost - smaller boost deltas should be irrelevant
				// NB: can't directly compare to tambient_boostdelta because target_ambient can be altered by indoor sensor
				if (target_ambient <= (aler(&circuit->run.target_ambient) - deltaK_to_temp(1)))
					circuit->run.floor_output = true;
			}
			break;
		case TRANS_NONE:
		default:
			break;
	}

	aser(&circuit->run.runmode, new_runmode);

	// reset output flooring when consumer_sdelay is unset  (XXX assumes consumer_sdelay will reach 0 between retriggers)
	if (circuit->run.floor_output && !circuit->pdata->run.consumer_sdelay)
		circuit->run.floor_output = false;

	// store current ambient & target temp
	aser(&circuit->run.actual_ambient, ambient_temp);
	aser(&circuit->run.target_ambient, target_ambient);

	dbgmsg(1, (circuit->run.transition), "\"%s\": Trans: %d, since: %u",
	       circuit->name, circuit->run.transition, timekeep_tk_to_sec(circuit->run.trans_start_time));

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
	}
	// startup is done.

	// Request for temp lower than (or equal) current: don't touch water_temp (let low request pass), update target to current
	else if (target_temp <= curr_temp) {
		circuit->run.rorh_last_target = curr_temp;	// update last_target to current point
		circuit->run.rorh_update_time = now;
	}
	// else: request for higher temp: apply rate limiter: target_temp is updated every HCIRCUIT_RORH_DT unless consumer_shift is negative in which case the algorithm pauses
	else {
		if (((now - circuit->run.rorh_update_time) >= HCIRCUIT_RORH_DT) && (circuit->pdata->run.consumer_shift >= 0)) {
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
 * @warning RM_TEST and RM_SUMMAINT bypass all safety logic.
 */
int hcircuit_run(struct s_hcircuit * const circuit)
{
	temp_t water_temp, curr_temp, ret_temp, lwtmin, lwtmax;
	int ret;

	if (unlikely(!circuit))
		return (-EINVALID);

	if (unlikely(!aler(&circuit->run.online)))	// implies set.configured == true
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

	// fetch limits
	lwtmin = SETorDEF(circuit->set.params.limit_wtmin, circuit->pdata->set.def_hcircuit.limit_wtmin);
	lwtmax = SETorDEF(circuit->set.params.limit_wtmax, circuit->pdata->set.def_hcircuit.limit_wtmax);

	// handle special runmode cases
	switch (aler(&circuit->run.runmode)) {
		case RM_OFF:
			if (circuit->run.active && circuit->run.floor_output) {	// executed at first switch from any mode to RM_OFF with floor_output
				// disable heat request from this circuit
				aser(&circuit->run.heat_request, RWCHCD_TEMP_NOREQUEST);
				water_temp = circuit->run.floor_wtemp;	// maintain last wtemp
				dbgmsg(2, 1, "\"%s\": in cooldown, remaining: %u", circuit->name, timekeep_tk_to_sec(circuit->pdata->run.consumer_sdelay));
				goto valveop;
			}
			else
				return (hcircuit_shutdown(circuit));
		case RM_TEST:
			valve_reqstop(circuit->set.p.valve_mix);	// in test mode, don't touch the valve (let the operator use it manually)
			goto summaint;
		case RM_SUMMAINT:
			valve_reqopen_full(circuit->set.p.valve_mix);	// in summer maintenance, open the valve in full
summaint:
			circuit->run.active = true;
			aser(&circuit->run.heat_request, RWCHCD_TEMP_NOREQUEST);
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
		ret = pump_set_state(circuit->set.p.pump_feed, ON, NOFORCE);
		if (unlikely(ALL_OK != ret)) {
			alarms_raise(ret, _("HCircuit \"%s\": failed to request feed pump \"%s\" ON"), circuit->name, pump_name(circuit->set.p.pump_feed));
			goto fail;
		}
	}

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

	// save "non-interfered" target water temp, i.e. the real target (within enforced limits) - needed by _logic()
	aser(&circuit->run.target_wtemp, water_temp);

	// heat request is always computed based on non-interfered water_temp value
	aser(&circuit->run.heat_request, water_temp + SETorDEF(circuit->set.params.temp_inoffset, circuit->pdata->set.def_hcircuit.temp_inoffset));

valveop:
	// alterations to the computed value only make sense if a mixing valve is available
	if (circuit->set.p.valve_mix) {
		// interference: apply rate of rise limitation if any
		// applied first so it's not impacted by the next interferences (in particular power shift). XXX REVIEW: might be needed to move after if ror control is desired on cshift rising edges
		if (circuit->set.wtemp_rorh)
			water_temp = hcircuit_ror_limiter(circuit, curr_temp, water_temp);

		// interference: handle output flooring requests: maintain previous or higher wtemp
		if (circuit->run.floor_output)
			water_temp = (water_temp > circuit->run.floor_wtemp) ? water_temp : circuit->run.floor_wtemp;
		else
			circuit->run.floor_wtemp = curr_temp;

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

		// XXX REVISIT: enforce lwtmin when frost is in effect? (this would bypass cshift)

		// adjust valve position if necessary
		ret = valve_mix_tcontrol(circuit->set.p.valve_mix, water_temp);
		if (unlikely(ret)) {
			alarms_raise(ret, _("HCircuit \"%s\": failed to control mixing valve \"%s\""), circuit->name, valve_name(circuit->set.p.valve_mix));
			goto fail;
		}
	}

#ifdef DEBUG
	(void)!inputs_temperature_get(circuit->set.tid_return, &ret_temp);
	dbgmsg(1, 1, "\"%s\": rq_amb: %.1f, tg_amb: %.1f, amb: %.1f, tg_wt: %.1f, tg_wt_mod: %.1f, cr_wt: %.1f, cr_rwt: %.1f, hrq_t: %.1f", circuit->name,
	       temp_to_celsius(aler(&circuit->run.request_ambient)), temp_to_celsius(aler(&circuit->run.target_ambient)), temp_to_celsius(aler(&circuit->run.actual_ambient)),
	       temp_to_celsius(aler(&circuit->run.target_wtemp)), temp_to_celsius(water_temp), temp_to_celsius(curr_temp), temp_to_celsius(ret_temp), temp_to_celsius(aler(&circuit->run.heat_request)));
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
		pr_err("\"%s\": bilinear inflexion point computation failed! (outinfl: %.2f, waterinfl: %.2f) - switching to linear mode",
		       circuit->name, temp_to_celsius(priv->run.toutinfl), temp_to_celsius(priv->run.twaterinfl));
		priv->run.toutinfl = (tout2 + tout1) / 2;
		priv->run.twaterinfl = (twater2 + twater1) / 2;
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

	freeconst(circuit->name);
	circuit->name = NULL;
	free(circuit->tlaw_priv);
	circuit->tlaw_priv = NULL;
}
