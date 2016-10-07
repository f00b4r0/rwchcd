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
int logic_circuit(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t target_temp;
	temp_t ambient_measured, ambient_delta;

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