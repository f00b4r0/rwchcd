//
//  rwchcd_logic.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

// smarter functions making use of time should be here and act as pre-filter for plant xxx_run() ops.

#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_logic.h"

/**
 * Conditions for running circuit
 * Circuit is off in ANY of the following conditions are met:
 * - t_outdoor_60 > current set_outhoff_MODE
 * - t_outdoor_mixed > current set_outhoff_MODE
 * - t_outdoor_attenuated > current set_outhoff_MODE
 * Circuit is back on if ALL of the following conditions are met:
 * - t_outdoor_60 < current set_outhoff_MODE - set_outhoff_histeresis
 * - t_outdoor_mixed < current set_outhoff_MODE - set_outhoff_histeresis
 * - t_outdoor_attenuated < current set_outhoff_MODE - set_outhoff_histeresis
 * State is preserved in all other cases
 */
static void circuit_outhoff(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t temp_trigger;

	switch (circuit->actual_runmode) {
		case RM_COMFORT:
			temp_trigger = circuit->set_outhoff_comfort;
			break;
		case RM_ECO:
			temp_trigger = circuit->set_outhoff_eco;
			break;
		case RM_FROSTFREE:
			temp_trigger = circuit->set_outhoff_frostfree;
			break;
		case RM_OFF:
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_MANUAL:
		case RM_UNKNOWN:
		default:
			return;	// XXX
	}

	if (!temp_trigger) {	// don't do anything if we have an invalid limit
		circuit->outhoff = false;
		return;	// XXX
	}

	if ((runtime->t_outdoor_60 > temp_trigger) ||
	    (runtime->t_outdoor_mixed > temp_trigger) ||
	    (runtime->t_outdoor_attenuated > temp_trigger)) {
		circuit->outhoff = true;
	}
	else {
		temp_trigger -= circuit->set_outhoff_histeresis;
		if ((runtime->t_outdoor_60 < temp_trigger) &&
		    (runtime->t_outdoor_mixed < temp_trigger) &&
		    (runtime->t_outdoor_attenuated < temp_trigger))
			circuit->outhoff = false;
	}
}


/**
 * Circuit logic.
 * @param circuit target circuit
 * @return exec status
 * XXX ADD optimizations (anticipated turn on/off, boost at turn on, accelerated cool down, max ambient... p36+)
 * XXX TODO ambient temp model (p37)
 */
int logic_circuit(struct s_heating_circuit * restrict const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t target_temp;
	temp_t ambient_measured, ambient_delta;
	
	if (!circuit)
		return (-EINVALID);
	
	if (!circuit->configured)
		return (-ENOTCONFIGURED);
	
	if (!circuit->online)
		return (-EOFFLINE);

	// handle global/local runmodes
	if (RM_AUTO == circuit->set_runmode)
		circuit->actual_runmode = runtime->runmode;
	else
		circuit->actual_runmode = circuit->set_runmode;

	// depending on circuit run mode, assess circuit target temp
	switch (circuit->actual_runmode) {
		case RM_OFF:
		case RM_MANUAL:
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			target_temp = circuit->set_tcomfort;
			break;
		case RM_ECO:
			target_temp = circuit->set_teco;
			break;
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = circuit->set_tfrostfree;
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// Check if the circuit meets outhoff conditions
	circuit_outhoff(circuit);
	// if the circuit does meet the conditions, turn it off: update runmode.
	if (circuit->outhoff)
		circuit->actual_runmode = RM_OFF;

	// save current ambient request
	circuit->request_ambient = target_temp;

	// XXX OPTIM if return temp is known

	// apply offset and save calculated target ambient temp to circuit
	circuit->target_ambient = circuit->request_ambient + circuit->set_toffset;

	// shift based on measured ambient temp (if available) influence p.41
	ambient_measured = get_temp(circuit->id_temp_ambient);
	if (validate_temp(ambient_measured) == ALL_OK) {
		ambient_delta = (circuit->set_ambient_factor/10) * (circuit->target_ambient - ambient_measured);
		circuit->target_ambient += ambient_delta;
	}

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
	temp_t target_temp;

	if (!dhwt)
		return (-EINVALID);
	
	if (!dhwt->configured)
		return (-ENOTCONFIGURED);
	
	if (!dhwt->online)
		return (-EOFFLINE);
	
	// handle global/local runmodes
	if (RM_AUTO == dhwt->set_runmode)
		dhwt->actual_runmode = runtime->dhwmode;
	else
		dhwt->actual_runmode = dhwt->set_runmode;
	
	// depending on dhwt run mode, assess dhwt target temp
	switch (dhwt->actual_runmode) {
		case RM_OFF:
		case RM_MANUAL:
			return (ALL_OK);	// No further processing
		case RM_COMFORT:
			target_temp = dhwt->set_tcomfort;
			break;
		case RM_ECO:
			target_temp = dhwt->set_teco;
			break;
		case RM_FROSTFREE:
			target_temp = dhwt->set_tfrostfree;
			break;
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}
	
	// enforce limits on dhw temp
	if (target_temp < dhwt->limit_tmin)
		target_temp = dhwt->limit_tmin;
	else if (target_temp > dhwt->limit_tmax)
		target_temp = dhwt->limit_tmax;
	
	// save current target dhw temp
	dhwt->target_temp = target_temp;
	
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
	temp_t temp, temp_request = 0;

	int ret = -ENOTIMPLEMENTED;
	
	if (!heat)
		return (-EINVALID);
	
	if (!heat->configured)
		return (-ENOTCONFIGURED);
	
	if (!heat->online)
		return (-EOFFLINE);

	// handle global/local runmodes
	if (RM_AUTO == heat->set_runmode)
		heat->actual_runmode = runtime->runmode;
	else
		heat->actual_runmode = heat->set_runmode;
	
	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (circuitl = runtime->plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		temp = circuitl->circuit->heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
	}
	
	// then dhwt
	for (dhwtl = runtime->plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		temp = dhwtl->dhwt->heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
	}
	
	// apply result to heat source
	heat->temp_request = temp_request;

	if (heat->hs_logic)
		ret = heat->hs_logic(heat);
	
	return (ret);
}