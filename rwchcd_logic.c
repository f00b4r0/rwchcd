//
//  rwchcd_logic.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Logic functions implementation for smart operation.
 * Smarter functions making use of time should be here and act as pre-filter for plant xxx_run() ops.
 */

#include <time.h>
#include <math.h>

#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_logic.h"

/**
 * Conditions for running circuit
 * The trigger temperature is the lowest of the set.outhoff_MODE and requested_ambient
 * Circuit is off in ANY of the following conditions are met:
 * - runtime->summer is true
 * - t_outdoor_60 > current temp_trigger
 * - t_outdoor_mixed > current temp_trigger
 * - t_outdoor_attenuated > current temp_trigger
 * Circuit is back on if ALL of the following conditions are met:
 * - runtime->summer is false
 * - t_outdoor_60 < current temp_trigger - outhoff_histeresis
 * - t_outdoor_mixed < current temp_trigger - outhoff_histeresis
 * - t_outdoor_attenuated < current temp_trigger - outhoff_histeresis
 * State is preserved in all other cases
 * @note This function needs run.request_ambient to be set prior calling for optimal operation
 */
static void circuit_outhoff(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t temp_trigger;

	// check for global summer switch off first
	if (runtime->summer) {
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
		case RM_MANUAL:
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
	    (runtime->t_outdoor_mixed > temp_trigger) ||
	    (runtime->t_outdoor_attenuated > temp_trigger)) {
		circuit->run.outhoff = true;
	}
	else {
		temp_trigger -= SETorDEF(circuit->set.params.outhoff_histeresis, runtime->config->def_circuit.outhoff_histeresis);
		if ((runtime->t_outdoor_60 < temp_trigger) &&
		    (runtime->t_outdoor_mixed < temp_trigger) &&
		    (runtime->t_outdoor_attenuated < temp_trigger))
			circuit->run.outhoff = false;
	}
}


/**
 * Circuit logic.
 * @param circuit target circuit
 * @return exec status
 * XXX TODO: ADD optimizations (anticipated turn on/off, max ambient... p36+)
 * XXX TODO: ambient max delta shutdown; optim based on return temp
 */
int logic_circuit(struct s_heating_circuit * restrict const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	enum e_runmode prev_runmode;
	temp_t request_temp, low_temp;
	temp_t ambient_temp = 0, ambient_delta = 0;
	time_t elapsed_time;
	const time_t now = time(NULL);
	
	if (!circuit)
		return (-EINVALID);
	
	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!circuit->run.online)
		return (-EOFFLINE);

	// store current status for transition detection
	prev_runmode = circuit->run.runmode;
	
	// handle global/local runmodes
	if (RM_AUTO == circuit->set.runmode)
		circuit->run.runmode = runtime->runmode;
	else
		circuit->run.runmode = circuit->set.runmode;

	// depending on circuit run mode, assess circuit target temp
	switch (circuit->run.runmode) {
		case RM_OFF:
		case RM_MANUAL:
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
	// if the circuit does meet the conditions, turn it off: update runmode.
	if (circuit->run.outhoff)
		circuit->run.runmode = RM_OFF;
	
	// transition detection
	if (prev_runmode != circuit->run.runmode) {
		circuit->run.transition = (circuit->run.actual_ambient > circuit->run.request_ambient) ? TRANS_DOWN : TRANS_UP;
		circuit->run.am_update_time = now;
	}

	// XXX OPTIM if return temp is known

	// apply offset and save calculated target ambient temp to circuit
	circuit->run.target_ambient = circuit->run.request_ambient + SETorDEF(circuit->set.params.t_offset, runtime->config->def_circuit.t_offset);

	// Ambient temperature is either read or modelled
	ambient_temp = get_temp(circuit->set.id_temp_ambient);
	if (validate_temp(ambient_temp) == ALL_OK) {	// we have an ambient sensor
		// calculate ambient shift based on measured ambient temp influence p.41
		ambient_delta = (circuit->set.ambient_factor/10) * (circuit->run.target_ambient - ambient_temp);
	}
	else {	// no sensor, apply ambient model for transitions
		elapsed_time = now - circuit->run.am_update_time;
		switch (circuit->run.transition) {
			case TRANS_DOWN:
				if (circuit->set.fast_cooldown)
					low_temp = runtime->t_outdoor_mixed;
				else
					low_temp = request_temp;
				// transition down, apply logarithmic cooldown model - XXX geared toward fast cooldown, will underestimate temp in ALL other cases REVIEW
				// all necessary data is _always_ available, no need to special case here
				if (elapsed_time >= 600) {	// XXX every 10mn
					ambient_temp = (circuit->run.actual_ambient - low_temp) * expf((float)-elapsed_time/(3*runtime->config->building_tau)) + low_temp;	// we converge toward low_temp
					circuit->run.am_update_time = now;
				}
				else
					ambient_temp = circuit->run.actual_ambient;
				break;
			case TRANS_UP:
				// transition up, apply linear model
				if (circuit->set.am_tambient_tK) {	// only if necessary data is available
					if (elapsed_time >= 600) {	// XXX every 10mn
						// current + (elapsed_time * 1/tperK) * (tboost / treq)
						ambient_temp = circuit->run.actual_ambient + (elapsed_time * 1/circuit->set.am_tambient_tK)*(1+circuit->set.tambient_boostdelta/request_temp);	// works even if boostdelta is not set
						circuit->run.am_update_time = now;
					}
					else
						ambient_temp = circuit->run.actual_ambient;
					break;
				}
				// if settings are insufficient, model can't run, fallback to no transition
			case TRANS_NONE:
				// no transition, ambient temp assumed to be request temp
				ambient_temp = circuit->run.request_ambient;
				circuit->run.am_update_time = 0;
				break;
			default:
				break;
		}
	}
	
	dbgmsg("Trans: %d, prev amb: %d, new amb: %d, elapsed: %d", circuit->run.transition, circuit->run.actual_ambient, ambient_temp, elapsed_time);
	
	// store current ambient temp
	circuit->run.actual_ambient = ambient_temp;

	// handle transitions
	switch (circuit->run.transition) {
		case TRANS_DOWN:
			if (ambient_temp > circuit->run.request_ambient) {
				if (circuit->set.fast_cooldown)	// if fast cooldown, turn off circuit
					circuit->run.runmode = RM_OFF;
			}
			else
				circuit->run.transition = TRANS_NONE;	// transition completed
			break;
		case TRANS_UP:
			if (ambient_temp < circuit->run.request_ambient - deltaK_to_temp(0.5F)) {	// boost if ambient temp is < to target - 0.5K - XXX
				// boost is max of set boost (if any) and measured delta (if any)
				ambient_delta = (circuit->set.tambient_boostdelta > ambient_delta) ? circuit->set.tambient_boostdelta : ambient_delta;
			}
			else
				circuit->run.transition = TRANS_NONE;	// transition completed
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
 * @param dhwt target dhwt
 * @return exec status
 */
int logic_dhwt(struct s_dhw_tank * restrict const dhwt)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t target_temp, ltmin, ltmax;

	if (!dhwt)
		return (-EINVALID);
	
	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!dhwt->run.online)
		return (-EOFFLINE);
	
	// handle global/local runmodes
	if (RM_AUTO == dhwt->set.runmode)
		dhwt->run.runmode = runtime->dhwmode;
	else
		dhwt->run.runmode = dhwt->set.runmode;
	
	// depending on dhwt run mode, assess dhwt target temp
	switch (dhwt->run.runmode) {
		case RM_OFF:
		case RM_MANUAL:
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

	// if anti-legionella charge is requested, enforce temp
	if (dhwt->run.legionella_on)	// XXX TODO: handle untrip
		target_temp = SETorDEF(dhwt->set.params.t_legionella, runtime->config->def_dhwt.t_legionella);

	// enforce limits on dhw temp
	ltmin = SETorDEF(dhwt->set.params.limit_tmin, runtime->config->def_dhwt.limit_tmin);
	ltmax = SETorDEF(dhwt->set.params.limit_tmax, runtime->config->def_dhwt.limit_tmax);
	if (target_temp < ltmin)
		target_temp = ltmin;
	else if (target_temp > ltmax)
		target_temp = ltmax;
	
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
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	temp_t temp, temp_request = RWCHCD_TEMP_NOREQUEST;
	const time_t now = time(NULL);

	int ret = -ENOTIMPLEMENTED;
	
	if (!heat)
		return (-EINVALID);
	
	if (!heat->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!heat->run.online)
		return (-EOFFLINE);

	// handle global/local runmodes
	if (RM_AUTO == heat->set.runmode)
		heat->run.runmode = runtime->runmode;
	else
		heat->run.runmode = heat->set.runmode;
	
	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (circuitl = runtime->plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		temp = circuitl->circuit->run.heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
		if (RWCHCD_TEMP_NOREQUEST != temp)
			heat->run.last_circuit_reqtime = now;
	}
	
	// check if last request exceeds timeout
	if ((heat->run.last_circuit_reqtime - now) > heat->set.sleeping_time)
		heat->run.could_sleep = true;
	else
		heat->run.could_sleep = false;
	
	// then dhwt
	for (dhwtl = runtime->plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		temp = dhwtl->dhwt->run.heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
	}
	
	// apply result to heat source
	heat->run.temp_request = temp_request;
	
	// XXX TODO: consumer stop delay should only be applied when heatsource temp is rising
	heat->run.target_consumer_stop_delay = heat->set.consumer_stop_delay;

	if (heat->hs_logic)
		ret = heat->hs_logic(heat);
	
	return (ret);
}