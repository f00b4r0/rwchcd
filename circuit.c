//
//  circuit.c
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Circuit operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>
#include <math.h>	// roundf

#include "circuit.h"
#include "hardware.h"
#include "lib.h"
#include "pump.h"
#include "valve.h"
#include "runtime.h"
#include "models.h"
#include "config.h"

#include "plant.h"

/**
 * Bilinear water temperature law.
 * This law approximates the curvature resulting from limited transmission non-linearities in heating elements
 * by splitting the curve in two linear segments around an inflexion point. It works well for 1 < nH < 1.5.
 * The target output water temperature is computed for a 20C target ambient. It is then shifted accordingly to
 * the actual target ambient temp, based on the original (linear) curve slope.
 * Most of these calculations are empirical "industry proven practices".
 *
 * https://pompe-a-chaleur.ooreka.fr/astuce/voir/111578/le-regulateur-loi-d-eau-pour-pompe-a-chaleur
 * http://www.energieplus-lesite.be/index.php?id=10959
 * http://herve.silve.pagesperso-orange.fr/regul.htm
 *
 * @param circuit self
 * @param source_temp outdoor temperature to consider
 * @return a target water temperature for this circuit
 * @warning no parameter check
 */
static temp_t templaw_bilinear(const struct s_heating_circuit * const circuit, const temp_t source_temp)
{
	const struct s_tlaw_bilin20C_priv * const tld = circuit->tlaw_data_priv;
	float slope;
	temp_t offset, t_output;

	assert(tld);

	// calculate new parameters based on current outdoor temperature (select adequate segment)
	if (source_temp < tld->toutinfl)
		slope = ((float)(tld->twaterinfl - tld->twater1)) / (tld->toutinfl - tld->tout1);
	else
		slope = ((float)(tld->twater2 - tld->twaterinfl)) / (tld->tout2 - tld->toutinfl);
	offset = tld->twaterinfl - (tld->toutinfl * slope);

	// calculate output at nominal 20C: Y = input*slope + offset
	t_output = roundf(source_temp * slope) + offset;

	dbgmsg("%s: orig: %.1f, new: %.1f", circuit->name, temp_to_celsius(roundf(source_temp * tld->slope) + tld->offset), temp_to_celsius(t_output));

	// shift output based on actual target temperature
	t_output += (circuit->run.target_ambient - celsius_to_temp(20)) * (1 - tld->slope);

	return (t_output);
}

/**
 * Put circuit online.
 * Perform all necessary actions to prepare the circuit for service but
 * DO NOT MARK IT AS ONLINE.
 * @param circuit target circuit
 * @param return exec status
 */
int circuit_online(struct s_heating_circuit * const circuit)
{
	temp_t testtemp;
	int ret;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->bmodel)
		return (-EMISCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(circuit->set.id_temp_outgoing);
	ret = validate_temp(testtemp);
	if (ret)
		goto out;

out:
	return (ret);
}

/**
 * Put circuit offline.
 * Perform all necessary actions to completely shut down the circuit but
 * DO NOT MARK IT AS OFFLINE.
 * @param circuit target circuit
 * @param return error status
 */
int circuit_offline(struct s_heating_circuit * const circuit)
{
	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	circuit->run.target_wtemp = 0;

	if (circuit->pump)
		pump_offline(circuit->pump);

	if (circuit->valve)
		valve_offline(circuit->valve);

	circuit->run.runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * Circuit failsafe routine.
 * By default we close the valve (if any) and start the pump (if any).
 * The logic being that we cannot make any assumption as to whether or not it is
 * safe to open the valve, whereas closing it will always be safe.
 * Turning on the pump mitigates frost risks.
 * @param circuit target circuit
 */
static void circuit_failsafe(struct s_heating_circuit * restrict const circuit)
{
	valve_reqclose_full(circuit->valve);
	if (circuit->pump)
		pump_set_state(circuit->pump, ON, FORCE);
}

/**
 * Circuit control loop.
 * Controls the circuits elements to achieve the desired target temperature.
 * @param circuit target circuit
 * @return exec status
 * @warning circuit->run.target_ambient must be properly set before this runs
 */
int circuit_run(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const time_t now = time(NULL);
	temp_t water_temp, curr_temp, saved_temp, lwtmin, lwtmax;
	bool interference = false;
	int ret;

	assert(circuit);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->run.online)
		return (-EOFFLINE);

	// handle special runmode cases
	switch (circuit->run.runmode) {
		case RM_OFF:
			if (circuit->run.target_wtemp && (runtime->plant->consumer_sdelay > 0)) {
				// disable heat request from this circuit
				circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				water_temp = circuit->run.target_wtemp;
				dbgmsg("%s: in cooldown, remaining: %ld", circuit->name, runtime->plant->consumer_sdelay);
				goto valve;	// stop processing
			}
			else
				return (circuit_offline(circuit));
		case RM_TEST:
			valve_reqstop(circuit->valve);
			if (circuit->pump)
				pump_set_state(circuit->pump, ON, FORCE);
			return (ALL_OK);
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the circuit is active

	// safety checks
	curr_temp = get_temp(circuit->set.id_temp_outgoing);
	ret = validate_temp(curr_temp);
	if (ALL_OK != ret) {
		circuit_failsafe(circuit);
		return (ret);
	}

	// we're good to go

	circuit->run.actual_wtemp = curr_temp;

	// circuit is active, ensure pump is running
	if (circuit->pump)
		pump_set_state(circuit->pump, ON, 0);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, circuit->bmodel->run.t_out_mix);

	// apply rate of rise limitation if any: update temp every minute
	if (circuit->set.wtemp_rorh) {
		if (!circuit->run.rorh_update_time) {	// first sample: init to current
			water_temp = curr_temp;
			circuit->run.rorh_last_target = water_temp;
			circuit->run.rorh_update_time = now;
		}
		else if (water_temp > curr_temp) {	// request for hotter water: apply rate only to rise
			if (now - circuit->run.rorh_update_time >= 60) {	// 1mn has past, update target - XXX 60s resolution
				curr_temp = temp_expw_mavg(circuit->run.rorh_last_target, circuit->run.rorh_last_target+circuit->set.wtemp_rorh, 3600, now - circuit->run.rorh_update_time);	// we hijack curr_temp here to save a variable
				water_temp = (curr_temp < water_temp) ? curr_temp : water_temp;	// target is min of circuit->templaw() and rorh-limited temp
				circuit->run.rorh_last_target = water_temp;
				circuit->run.rorh_update_time = now;
			}
		}
		else {	// request for cooler or same temp
			circuit->run.rorh_last_target = curr_temp;	// update last target to current temp so that the next hotter run starts from "current position"
			circuit->run.rorh_update_time = now;
		}
	}

	// enforce limits
	lwtmin = SETorDEF(circuit->set.params.limit_wtmin, runtime->config->def_circuit.limit_wtmin);
	lwtmax = SETorDEF(circuit->set.params.limit_wtmax, runtime->config->def_circuit.limit_wtmax);

	// low limit can be overriden by external interferences
	if (water_temp < lwtmin)
		water_temp = lwtmin;

	// save "non-interfered" target water temp
	saved_temp = water_temp;

	// interference: handle output flooring requests: maintain current or higher wtemp
	if (circuit->run.floor_output) {
		water_temp = (water_temp > circuit->run.target_wtemp) ? water_temp : circuit->run.target_wtemp;
		interference = true;
	}

	// interference: apply global shift
	if (runtime->plant->consumer_shift) {
		// X% shift is (current + X*(current - ref)/100). ref is 0°C (absolute physical minimum) to avoid potential inversion problems with return temp
		water_temp += runtime->plant->consumer_shift * (water_temp - celsius_to_temp(0)) / 100;
		interference = true;
	}

	// high limit can never be overriden
	if (water_temp > lwtmax)
		water_temp = lwtmax;
	if (saved_temp > lwtmax)
		saved_temp = lwtmax;

	dbgmsg("%s: request_amb: %.1f, target_amb: %.1f, target_wt: %.1f, curr_wt: %.1f, curr_rwt: %.1f", circuit->name,
	       temp_to_celsius(circuit->run.request_ambient), temp_to_celsius(circuit->run.target_ambient),
	       temp_to_celsius(water_temp), temp_to_celsius(get_temp(circuit->set.id_temp_outgoing)),
	       temp_to_celsius(get_temp(circuit->set.id_temp_return)));

	// heat request is always computed based on non-interfered water_temp value
	circuit->run.heat_request = saved_temp + SETorDEF(circuit->set.params.temp_inoffset, runtime->config->def_circuit.temp_inoffset);

	// in the absence of external "interference", update saved target water temp
	// note: this is necessary to avoid storing the new, cooler saved_temp during TRANS_DOWN cooldown
	if (!interference)
		circuit->run.target_wtemp = saved_temp;

valve:
	// adjust valve position if necessary
	if (circuit->valve && circuit->valve->set.configured) {
		ret = valve_control(circuit->valve, water_temp);
		if (ret && (ret != -EDEADZONE))	// return error code if it's not EDEADZONE
			goto out;
	}

	// if we want to add a check for nominal power reached: if ((-EDEADZONE == ret) || (get_temp(circuit->set.id_temp_outgoing) > circuit->run.target_ambient))

	ret = ALL_OK;
out:
	return (ret);
}

/**
 * Assign bilinear temperature law to the circuit.
 * This function is used to assign or update a bilinear temperature law (and its
 * associated parameters) to a target circuit.
 * To determine the position of the inflexion point, the calculation starts from the linear curve as determined
 * by the two set points. It then computes the outdoor temperature corresponding to a 20C water output temp.
 * Then, it computes the temperature differential between the lowest outdoor temp set point and that calculated value.
 * The inflexion point is located on that differential, 30% down from the 20C output water temp point.
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
int circuit_make_bilinear(struct s_heating_circuit * const circuit,
			  temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, int_fast16_t nH100)
{
	struct s_tlaw_bilin20C_priv * priv = NULL;
	temp_t toutw20C, tlin;

	if (!circuit)
		return (-EINVALID);

	// validate input
	if ((tout1 >= tout2) || (twater1 <= twater2))
		return (-EINVALID);

	// create priv element if it doesn't already exist
	if (!circuit->tlaw_data_priv) {
		priv = calloc(1, sizeof(*priv));
		if (!priv)
			return (-EOOM);
	}
	else if ((templaw_bilinear == circuit->templaw) && circuit->tlaw_data_priv)
		priv = circuit->tlaw_data_priv;
	else
		return (-EINVALID);

	priv->tout1 = tout1;
	priv->twater1 = twater1;
	priv->tout2 = tout2;
	priv->twater2 = twater2;
	priv->nH100 = nH100;

	// calculate the linear slope = (Y2 - Y1)/(X2 - X1)
	priv->slope = ((float)(priv->twater2 - priv->twater1)) / (priv->tout2 - priv->tout1);
	// offset: reduce through a known point
	priv->offset = priv->twater2 - (priv->tout2 * priv->slope);

	if (!priv->toutinfl) {
		// calculate outdoor temp for 20C water temp
		toutw20C = roundf(((float)(celsius_to_temp(20) - priv->offset)) / priv->slope);

		// calculate outdoor temp for inflexion point (toutw20C - (30% of toutw20C - tout1))
		priv->toutinfl = toutw20C - ((toutw20C - priv->tout1) * 30 / 100);

		// calculate corrected water temp at inflexion point (tlinear[nH=1] - 20C) * (nH - 1)
		tlin = (roundf(priv->toutinfl * priv->slope) + priv->offset);
		priv->twaterinfl = tlin + ((tlin - celsius_to_temp(20)) * (priv->nH100 - 100) / 100);
	}

	// attach priv structure
	circuit->tlaw_data_priv = priv;

	circuit->templaw = templaw_bilinear;

	return (ALL_OK);
}

/**
 * Circuit destructor.
 * Frees all circuit-local resources
 * @param circuit the circuit to delete
 */
void circuit_del(struct s_heating_circuit * circuit)
{
	if (!circuit)
		return;

	free(circuit->name);
	circuit->name = NULL;

	free(circuit);
}