//
//  logic.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Logic functions implementation for smart operation.
 * Smarter functions making use of time should be here and act as pre-filter for plant xxx_run() ops.
 * @todo implement a flexible logic system that would take user-definable conditions and user-selectable actions to trigger custom actions (for more flexible plants)
 */

#include <time.h>
#include <math.h>
#include <assert.h>

#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "logic.h"
#include "models.h"

#include "circuit.h"
#include "dhwt.h"
#include "heatsource.h"

/**
 * Conditions for running circuit
 * The trigger temperature is the lowest of the set.outhoff_MODE and requested_ambient
 * Circuit is off in ANY of the following conditions are met:
 * - runtime->summer is true
 * - t_out_60 > current temp_trigger
 * - t_out_mix > current temp_trigger
 * - t_out_att > current temp_trigger
 * Circuit is back on if ALL of the following conditions are met:
 * - runtime->summer is false
 * - t_out_60 < current temp_trigger - outhoff_histeresis
 * - t_out_mix < current temp_trigger - outhoff_histeresis
 * - t_out_att < current temp_trigger - outhoff_histeresis
 * State is preserved in all other cases
 * @note This function needs run.request_ambient to be set prior calling for optimal operation
 */
static void circuit_outhoff(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const struct s_bmodel * restrict const bmodel = circuit->bmodel;
	temp_t temp_trigger;

	// check for summer switch off first
	if (bmodel->run.summer) {
		circuit->run.outhoff = true;
		return;
	}
	
	switch (circuit->run.runmode) {
		case RM_COMFORT:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_comfort, runtime->config->def_circuit.outhoff_comfort);
			break;
		case RM_ECO:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_eco, runtime->config->def_circuit.outhoff_eco);
			break;
		case RM_DHWONLY:
		case RM_FROSTFREE:
			temp_trigger = SETorDEF(circuit->set.params.outhoff_frostfree, runtime->config->def_circuit.outhoff_frostfree);
			break;
		case RM_OFF:
		case RM_AUTO:
		case RM_TEST:
		case RM_UNKNOWN:
		default:
			return;
	}
	
	// min of setting and current ambient request
	temp_trigger = (circuit->run.request_ambient < temp_trigger) ? circuit->run.request_ambient : temp_trigger;

	if (!temp_trigger) {	// don't do anything if we have an invalid limit
		circuit->run.outhoff = false;
		return;
	}

	if ((runtime->t_outdoor_60 > temp_trigger) ||
	    (bmodel->run.t_out_mix > temp_trigger) ||
	    (bmodel->run.t_out_att > temp_trigger)) {
		circuit->run.outhoff = true;
	}
	else {
		temp_trigger -= SETorDEF(circuit->set.params.outhoff_histeresis, runtime->config->def_circuit.outhoff_histeresis);
		if ((runtime->t_outdoor_60 < temp_trigger) &&
		    (bmodel->run.t_out_mix < temp_trigger) &&
		    (bmodel->run.t_out_att < temp_trigger))
			circuit->run.outhoff = false;
	}
}


/**
 * Circuit logic.
 * Sets the target ambient temperature for a circuit based on selected run mode.
 * Runs the ambient model, and applies temperature shift based on mesured or
 * modelled ambient temperature. Handles runmode transitions.
 * @param circuit target circuit
 * @return exec status
 * @todo cleanup
 * @todo XXX TODO: ADD optimizations (anticipated turn on/off, max ambient... p36+)
 * @todo XXX TODO: ambient max delta shutdown; optim based on return temp
 * @todo XXX TODO: optimization with return temperature
 * @note the ambient model has a hackish acknowledgment of lag due to circuit warming up
 * (including rate of rise limitation). REVIEW
 */
int logic_circuit(struct s_heating_circuit * restrict const circuit)
{
#define LOGIC_MIN_POWER_TRANS_UP	75	///< minimum estimate (linear) output power percentage for transition up modelling
	const struct s_runtime * restrict const runtime = get_runtime();
	const struct s_bmodel * restrict const bmodel = circuit->bmodel;
	enum e_runmode prev_runmode;
	temp_t request_temp, diff_temp;
	temp_t ambient_temp, ambient_delta = 0;
	time_t elapsed_time, dtmin;
	const time_t now = time(NULL);
	bool can_fastcool;
	
	assert(bmodel);
	
	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!circuit->run.online)
		return (-EOFFLINE);
	
	// fast cooldown can only be applied if set AND not in frost condition
	can_fastcool = (circuit->set.fast_cooldown && !runtime->frost);

	// store current status for transition detection
	prev_runmode = circuit->run.runmode;
	
	// handle global/local runmodes
	if (RM_AUTO == circuit->set.runmode)
		circuit->run.runmode = runtime->runmode;
	else
		circuit->run.runmode = circuit->set.runmode;

	// if an absolute priority DHW charge is in progress, switch to dhw-only (will register the transition)
	if (runtime->dhwc_absolute)
		circuit->run.runmode = RM_DHWONLY;

	// depending on circuit run mode, assess circuit target temp
	switch (circuit->run.runmode) {
		case RM_OFF:
		case RM_TEST:
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			request_temp = SETorDEF(circuit->set.params.t_comfort, runtime->config->def_circuit.t_comfort);
			break;
		case RM_ECO:
			request_temp = SETorDEF(circuit->set.params.t_eco, runtime->config->def_circuit.t_eco);
			break;
		case RM_DHWONLY:
		case RM_FROSTFREE:
			request_temp = SETorDEF(circuit->set.params.t_frostfree, runtime->config->def_circuit.t_frostfree);
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}
	
	// save current ambient request
	circuit->run.request_ambient = request_temp;

	// Check if the circuit meets run.outhoff conditions
	circuit_outhoff(circuit);
	// if the circuit does meet the conditions (and frost is not in effect), turn it off: update runmode.
	if (circuit->run.outhoff && !runtime->frost)
		circuit->run.runmode = RM_OFF;

	// transition detection - check actual_ambient to avoid false trigger at e.g. startup
	if ((prev_runmode != circuit->run.runmode) && circuit->run.actual_ambient) {
		circuit->run.transition = (circuit->run.actual_ambient > circuit->run.request_ambient) ? TRANS_DOWN : TRANS_UP;
		circuit->run.trans_since = now;
		circuit->run.trans_start_temp = circuit->run.actual_ambient;
		circuit->run.trans_active_elapsed = 0;
	}

	// handle extra logic
	// floor output during down transition if requested by the plant, except when absolute DHWT priority charge is in effect
	if ((TRANS_DOWN == circuit->run.transition) && runtime->consumer_sdelay && !runtime->dhwc_absolute)
		circuit->run.floor_output = true;

	// reset output flooring ONLY when sdelay is elapsed (avoid early reset if transition ends early)
	if (!runtime->consumer_sdelay)
		circuit->run.floor_output = false;

	// XXX OPTIM if return temp is known

	// apply offset and save calculated target ambient temp to circuit
	circuit->run.target_ambient = circuit->run.request_ambient + SETorDEF(circuit->set.params.t_offset, runtime->config->def_circuit.t_offset);

	elapsed_time = now - circuit->run.ambient_update_time;

	// Ambient temperature is either read or modelled
	ambient_temp = get_temp(circuit->set.id_temp_ambient);
	if (validate_temp(ambient_temp) == ALL_OK) {	// we have an ambient sensor
		// calculate ambient shift based on measured ambient temp influence in percent
		ambient_delta = (circuit->set.ambient_factor) * (circuit->run.target_ambient - ambient_temp) / 100;
	}
	else {	// no sensor (or faulty), apply ambient model
		ambient_temp = circuit->run.actual_ambient;
		dtmin = expw_mavg_dtmin(3*bmodel->set.tau);
		
		// if circuit is OFF (due to outhoff()) apply moving average based on outdoor temp
		if (RM_OFF == circuit->run.runmode) {
			if (elapsed_time > dtmin) {
				ambient_temp = temp_expw_mavg(circuit->run.actual_ambient, bmodel->run.t_out_mix, 3*bmodel->set.tau, elapsed_time); // we converge toward low_temp
				circuit->run.ambient_update_time = now;
			}
			dbgmsg("%s: off, ambient: %.1f", circuit->name, temp_to_celsius(ambient_temp));
		}
		else {
			// otherwise apply transition models. Circuit cannot be RM_OFF here
			switch (circuit->run.transition) {
				case TRANS_DOWN:
					// transition down, apply logarithmic cooldown model - XXX geared toward fast cooldown, will underestimate temp in ALL other cases REVIEW
					// all necessary data is _always_ available, no need to special case here
					if (elapsed_time > dtmin) {
						ambient_temp = temp_expw_mavg(circuit->run.actual_ambient, request_temp, 3*bmodel->set.tau, elapsed_time); // we converge toward low_temp
						circuit->run.ambient_update_time = now;
						circuit->run.trans_active_elapsed += elapsed_time;
					}
					break;
				case TRANS_UP:
					// transition up, apply semi-exponential model
					if (circuit->set.am_tambient_tK) {	// only if necessary data is available
						/* XXX count active time only if approximate output power is > LOGIC_MIN_POWER_TRANS_UP %
						  (linear) approximate output power is (actual_wtemp - ambient) / (target_wtemp - ambient) */
						if ((100 * (circuit->run.actual_wtemp - circuit->run.actual_ambient) / (circuit->run.target_wtemp - circuit->run.actual_ambient)) > LOGIC_MIN_POWER_TRANS_UP)
							circuit->run.trans_active_elapsed += elapsed_time;

						// tstart + elevation over time: tstart + ((elapsed_time * KPRECISIONI/tperK) * ((treq - tcurrent + tboost) / (treq - tcurrent)))
						/* note: the impact of the boost should be considered as a percentage of the total
						 requested temperature increase over _current_ temp, hence (treq - tcurrent).
						 Furthermore, by simply adjusting a few factors in equal proportion (KPRECISIONI),
						 we don't need to deal with floats and we can keep a good precision.
						 Also note that am_tambient_tK must be considered /KPRECISIONI to match the internal
						 temp type which is K*KPRECISIONI.
						 IMPORTANT: it is essential that this computation be stopped when the
						 temperature differential (request - actual) is < KPRECISIONI (1K) otherwise the
						 term that tends toward 0 introduces a huge residual error when boost is enabled.
						 If TRANS_UP is run when request == actual, the computation would trigger a divide by 0 (SIGFPE) */
						diff_temp = request_temp - circuit->run.actual_ambient;
						if (diff_temp >= KPRECISIONI) {
							ambient_temp = circuit->run.trans_start_temp + (((KPRECISIONI*circuit->run.trans_active_elapsed / circuit->set.am_tambient_tK) *
									(KPRECISIONI + (KPRECISIONI*circuit->set.tambient_boostdelta) / diff_temp)) / KPRECISIONI);	// works even if boostdelta is not set
						}
						else
							ambient_temp = request_temp;
						circuit->run.ambient_update_time = now;
						break;
					}
					// if settings are insufficient, model can't run, fallback to no transition
				case TRANS_NONE:
					// no transition, ambient temp assumed to be request temp
					ambient_temp = circuit->run.request_ambient;
					circuit->run.ambient_update_time = now;
					break;
				default:
					break;
			}
			if (circuit->run.transition)	// elapsed_time can be uninitialized once in this dbgmsg(). We don't care
				dbgmsg("%s: Trans: %d, start amb: %d, curr amb: %d, active elapsed: %ld",
				       circuit->name, circuit->run.transition, circuit->run.trans_start_temp, ambient_temp, circuit->run.trans_active_elapsed);
		}
	}
	
	// store current ambient temp
	circuit->run.actual_ambient = ambient_temp;

	// handle transitions
	switch (circuit->run.transition) {
		case TRANS_DOWN:
			if (ambient_temp > (circuit->run.request_ambient + deltaK_to_temp(0.5F))) {
				if (can_fastcool)	// if fast cooldown is possible, turn off circuit
					circuit->run.runmode = RM_OFF;
			}
			else {
				circuit->run.transition = TRANS_NONE;	// transition completed
				circuit->run.trans_since = 0;
			}
			break;
		case TRANS_UP:
			if (ambient_temp < (circuit->run.request_ambient - deltaK_to_temp(1.0F))) {	// boost if ambient temp < (target - 1K) - Note see 'IMPORTANT' above
				// boost is max of set boost (if any) and measured delta (if any)
				if ((now - circuit->run.trans_since) < circuit->set.max_boost_time)
					ambient_delta = (circuit->set.tambient_boostdelta > ambient_delta) ? circuit->set.tambient_boostdelta : ambient_delta;
			}
			else {
				circuit->run.transition = TRANS_NONE;	// transition completed
				circuit->run.trans_since = 0;
			}
			break;
		case TRANS_NONE:
		default:
			break;
	}

	// apply ambient shift
	circuit->run.target_ambient += ambient_delta;

	return (ALL_OK);
}

/**
 * DHWT logic.
 * Sets DHWT target temperature based on selected run mode.
 * Enforces programmatic use of force charge when necessary.
 * @param dhwt target dhwt
 * @return exec status
 * @todo handle legionella correctly
 */
int logic_dhwt(struct s_dhw_tank * restrict const dhwt)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const time_t now = time(NULL);
	const struct tm * const ltime = localtime(&now);	// localtime handles DST and TZ for us
	enum e_runmode prev_runmode;
	temp_t target_temp, ltmin, ltmax;

	assert(dhwt);
	
	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!dhwt->run.online)
		return (-EOFFLINE);
	
	// store current status for transition detection
	prev_runmode = dhwt->run.runmode;
	
	// handle global/local runmodes
	if (RM_AUTO == dhwt->set.runmode)
		dhwt->run.runmode = runtime->dhwmode;
	else
		dhwt->run.runmode = dhwt->set.runmode;
	
	// depending on dhwt run mode, assess dhwt target temp
	switch (dhwt->run.runmode) {
		case RM_OFF:
		case RM_TEST:
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			target_temp = SETorDEF(dhwt->set.params.t_comfort, runtime->config->def_dhwt.t_comfort);
			break;
		case RM_ECO:
			target_temp = SETorDEF(dhwt->set.params.t_eco, runtime->config->def_dhwt.t_eco);
			break;
		case RM_FROSTFREE:
			target_temp = SETorDEF(dhwt->set.params.t_frostfree, runtime->config->def_dhwt.t_frostfree);
			break;
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if anti-legionella charge is requested, enforce temp and bypass logic
	if (dhwt->run.legionella_on) {	// XXX TODO: handle untrip
		target_temp = SETorDEF(dhwt->set.params.t_legionella, runtime->config->def_dhwt.t_legionella);
		dhwt->run.force_on = true;
		dhwt->run.recycle_on = true;
		goto settarget;
	}

	// transition detection
	if (prev_runmode != dhwt->run.runmode) {
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
	ltmin = SETorDEF(dhwt->set.params.limit_tmin, runtime->config->def_dhwt.limit_tmin);
	ltmax = SETorDEF(dhwt->set.params.limit_tmax, runtime->config->def_dhwt.limit_tmax);
	if (target_temp < ltmin)
		target_temp = ltmin;
	else if (target_temp > ltmax)
		target_temp = ltmax;

settarget:
	// save current target dhw temp
	dhwt->run.target_temp = target_temp;
	
	return (ALL_OK);
}

/**
 * Heat source logic.
 * @param heat target heat source
 * @return exec status
 */
int logic_heatsource(struct s_heatsource * restrict const heat)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const time_t now = time(NULL);
	const time_t dt = now - heat->run.last_run_time;
	temp_t temp;
	int ret = -ENOTIMPLEMENTED;
	
	assert(heat);
	
	if (!heat->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!heat->run.online)
		return (-EOFFLINE);

	// handle global/local runmodes
	if (RM_AUTO == heat->set.runmode)
		heat->run.runmode = runtime->runmode;
	else
		heat->run.runmode = heat->set.runmode;

	heat->run.temp_request = runtime->plant_hrequest;
	heat->run.could_sleep = runtime->plant_could_sleep;

	// compute sliding integral in DHW sliding prio
	if (runtime->dhwc_sliding) {
		temp = temp_thrs_intg(&heat->run.sld_itg, heat->run.temp_request, heat->cb.temp_out(heat), runtime->temps_time);

		if (temp < 0) {
			// percentage of shift is formed by the integral of current temp vs expected temp: 1Ks is -1% shift
			heat->run.cshift_noncrit = temp / KPRECISIONI;
		}
		else {
			heat->run.cshift_noncrit = 0;	// reset shift
			heat->run.sld_itg.integral = 0;	// reset integral
		}

	}

	// decrement consummer stop delay if any
	if (dt < heat->run.target_consumer_sdelay)
		heat->run.target_consumer_sdelay -= dt;
	else
		heat->run.target_consumer_sdelay = 0;

	if (heat->cb.logic)
		ret = heat->cb.logic(heat);

	heat->run.last_run_time = now;
	
	return (ret);
}
