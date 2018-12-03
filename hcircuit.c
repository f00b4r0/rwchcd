//
//  hcircuit.c
//  rwchcd
//
//  (C) 2017-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>
#include <math.h>	// roundf

#include "hcircuit.h"
#include "hardware.h"
#include "lib.h"
#include "pump.h"
#include "valve.h"
#include "runtime.h"
#include "models.h"
#include "config.h"
#include "log.h"


/**
 * Heating circuit data log callback.
 * @warning uses statically allocated data, must not be called concurrently.
 * @param ldata the log data to populate
 * @param object the opaque pointer to heating circuit structure
 * @return exec status
 */
static int hcircuit_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_hcircuit * const circuit = object;
	static const log_key_t keys[] = {
		"runmode", "request_ambient", "target_ambient", "actual_ambient", "target_wtemp", "actual_wtemp", "heat_request",
	};
	static log_value_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;

	assert(ldata);

	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->run.online)
		return (-EOFFLINE);

	values[i++] = circuit->run.runmode;
	values[i++] = circuit->run.request_ambient;
	values[i++] = circuit->run.target_ambient;
	values[i++] = circuit->run.actual_ambient;
	values[i++] = circuit->run.target_wtemp;
	values[i++] = circuit->run.actual_wtemp;
	values[i++] = circuit->run.heat_request;

	ldata->keys = keys;
	ldata->values = values;
	ldata->nkeys = ARRAY_SIZE(keys);
	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for a given circuit.
 * @param circuit the target circuit
 * @return (statically allocated) s_log_source pointer
 */
static const struct s_log_source * hcircuit_lreg(const struct s_hcircuit * const circuit)
{
	const log_version_t version = 1;
	static struct s_log_source Hcircuit_lreg;

	Hcircuit_lreg = (struct s_log_source){
		.log_sched = LOG_SCHED_5mn,
		.basename = "hcircuit_",
		.identifier = circuit->name,
		.version = version,
		.logdata_cb = hcircuit_logdata_cb,
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

	if (!circuit->set.logging)
		return (ALL_OK);

	return (log_register(hcircuit_lreg(circuit)));
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

	if (!circuit->set.logging)
		return (ALL_OK);

	return (log_deregister(hcircuit_lreg(circuit)));
}

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
static temp_t templaw_bilinear(const struct s_hcircuit * const circuit, const temp_t source_temp)
{
	const struct s_tlaw_bilin20C_priv * const tld = circuit->tlaw_priv;
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

	dbgmsg("\"%s\": lin: %.1f, comp: %.1f", circuit->name, temp_to_celsius(roundf(source_temp * tld->slope) + tld->offset), temp_to_celsius(t_output));

	// shift output based on actual target temperature
	t_output += (circuit->run.target_ambient - celsius_to_temp(20)) * (1 - tld->slope);

	return (t_output);
}

/**
 * Create a circuit
 * @return the newly created circuit or NULL
 */
struct s_hcircuit * hcircuit_new(void)
{
	struct s_hcircuit * const circuit = calloc(1, sizeof(*circuit));
	return (circuit);
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
	const struct s_runtime * restrict const runtime = runtime_get();
	temp_t temp;
	int ret;

	assert(runtime);

	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->bmodel || !circuit->templaw)
		return (-EMISCONFIGURED);

	// check that mandatory sensors are set
	ret = hardware_sensor_clone_time(circuit->set.tid_outgoing, NULL);
	if (ret)
		goto out;

	// limit_wtmax must be > 0C
	temp = SETorDEF(circuit->set.params.limit_wtmax, runtime->config->def_hcircuit.limit_wtmax);
	if (temp <= celsius_to_temp(0))
		ret = -EMISCONFIGURED;

	// make sure associated building model is configured
	if (!circuit->bmodel || !circuit->bmodel->set.configured) {
		dbgerr("\"%s\": building model not configured", circuit->name);
		ret = -EMISCONFIGURED;
	}
	// if pump exists check it's correctly configured
	if (circuit->pump && !circuit->pump->set.configured) {
		dbgerr("\"%s\": pump \"%s\" not configured", circuit->name, circuit->pump->name);
		ret = -EMISCONFIGURED;
	}

	// log registration shouldn't cause online failure
	if (hcircuit_log_register(circuit) != ALL_OK)
		dbgerr("\"%s\": couldn't register for logging", circuit->name);

	if (ALL_OK == ret)
		circuit->run.online = true;

out:
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

	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	circuit->run.target_wtemp = 0;

	if (circuit->pump)
		pump_offline(circuit->pump);

	if (circuit->valve)
		valve_offline(circuit->valve);

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

	hcircuit_log_deregister(circuit);

	circuit->run.runmode = RM_OFF;
	circuit->run.online = false;

	return (hcircuit_shutdown(circuit));
}

/**
 * Circuit failsafe routine.
 * By default we shutdown the circuit:
 * - remove heat request
 * - close the valve (if any)
 * - start the pump (if any)
 * The logic being that we cannot make any assumption as to whether or not it is
 * safe to open the valve, whereas closing it will always be safe.
 * Turning on the pump mitigates frost risks.
 * @param circuit target circuit
 */
static void hcircuit_failsafe(struct s_hcircuit * restrict const circuit)
{
	assert(circuit);
	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
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
int hcircuit_run(struct s_hcircuit * const circuit)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	const time_t now = time(NULL);
	temp_t water_temp, curr_temp, ret_temp, saved_temp, lwtmin, lwtmax;
#ifdef DEBUG
	temp_t out_temp;
#endif
	bool interference = false;
	int ret;

	assert(runtime);

	if (!circuit)
		return (-EINVALID);

	if (!circuit->run.online)	// implies set.configured == true
		return (-EOFFLINE);

	// safety checks
	ret = hardware_sensor_clone_temp(circuit->set.tid_outgoing, &curr_temp);
	if (ALL_OK != ret) {
		hcircuit_failsafe(circuit);
		return (ret);
	}

	// we're good to go - keep updating actual_wtemp when circuit is off
	circuit->run.actual_wtemp = curr_temp;

	// handle special runmode cases
	switch (circuit->run.runmode) {
		case RM_OFF:
			if (circuit->run.target_wtemp && (runtime->consumer_sdelay > 0)) {
				// disable heat request from this circuit
				circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				water_temp = circuit->run.target_wtemp;
				dbgmsg("\"%s\": in cooldown, remaining: %ld", circuit->name, runtime->consumer_sdelay);
				goto valve;	// stop processing
			}
			else
				return (hcircuit_shutdown(circuit));
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

	// if building model isn't online, failsafe
	if (!circuit->bmodel->run.online) {
		hcircuit_failsafe(circuit);
		return (-ESAFETY);
	}

	// circuit is active, ensure pump is running
	if (circuit->pump) {
		ret = pump_set_state(circuit->pump, ON, 0);
		if (ALL_OK != ret) {
			dbgerr("\"%s\": failed to set pump \"%s\" ON (%d)", circuit->name, circuit->pump->name, ret);
			hcircuit_failsafe(circuit);
			return (ret);	// critical error: stop there
		}
	}

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
			if (now - circuit->run.rorh_update_time >= 60) {	// 1mn has past, update target - XXX hardcoded 60s resolution
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
	lwtmin = SETorDEF(circuit->set.params.limit_wtmin, runtime->config->def_hcircuit.limit_wtmin);
	lwtmax = SETorDEF(circuit->set.params.limit_wtmax, runtime->config->def_hcircuit.limit_wtmax);

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

	// interference: apply global power shift
	if (runtime->consumer_shift) {
		ret = hardware_sensor_clone_temp(circuit->set.tid_return, &ret_temp);
		// if we don't have a return temp or if the return temp is higher than the outgoing temp, use 0°C (absolute physical minimum) as reference
		if ((ALL_OK != ret) || (ret_temp >= water_temp))
			ret_temp = celsius_to_temp(0);

		// X% shift is (current + X*(current - ref)/100). ref is return temp
		water_temp += runtime->consumer_shift * (water_temp - ret_temp) / 100;
		interference = true;
	}

	// high limit can never be overriden
	if (water_temp > lwtmax)
		water_temp = lwtmax;
	if (saved_temp > lwtmax)
		saved_temp = lwtmax;

#ifdef DEBUG
	hardware_sensor_clone_temp(circuit->set.tid_return, &ret_temp);
	hardware_sensor_clone_temp(circuit->set.tid_outgoing, &out_temp);
	dbgmsg("\"%s\": rq_amb: %.1f, tg_amb: %.1f, tg_wt: %.1f, cr_wt: %.1f, cr_rwt: %.1f", circuit->name,
	       temp_to_celsius(circuit->run.request_ambient), temp_to_celsius(circuit->run.target_ambient),
	       temp_to_celsius(water_temp), temp_to_celsius(out_temp), temp_to_celsius(ret_temp));
#endif

	// heat request is always computed based on non-interfered water_temp value
	circuit->run.heat_request = saved_temp + SETorDEF(circuit->set.params.temp_inoffset, runtime->config->def_hcircuit.temp_inoffset);

	// in the absence of external "interference", update saved target water temp
	// note: this is necessary to avoid storing the new, cooler saved_temp during TRANS_DOWN cooldown
	if (!interference)
		circuit->run.target_wtemp = saved_temp;

valve:
	// adjust valve position if necessary
	if (circuit->valve) {
		ret = valve_tcontrol(circuit->valve, water_temp);
		if (ret && (ret != -EDEADZONE))	// return error code if it's not EDEADZONE
			goto out;
	}

	// if we want to add a check for nominal power reached: if ((-EDEADZONE == ret) || (get_temp(circuit->set.tid_outgoing) > circuit->run.target_ambient))

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
int circuit_make_bilinear(struct s_hcircuit * const circuit,
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
	if (!circuit->tlaw_priv) {
		priv = calloc(1, sizeof(*priv));
		if (!priv)
			return (-EOOM);
	}
	else if ((HCL_BILINEAR == circuit->set.tlaw) && circuit->tlaw_priv)
		priv = circuit->tlaw_priv;
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
	circuit->tlaw_priv = priv;

	circuit->templaw = templaw_bilinear;

	circuit->set.tlaw = HCL_BILINEAR;

	return (ALL_OK);
}

/**
 * Circuit destructor.
 * Frees all circuit-local resources
 * @param circuit the circuit to delete
 */
void hcircuit_del(struct s_hcircuit * circuit)
{
	if (!circuit)
		return;

	free(circuit->name);
	circuit->name = NULL;

	free(circuit);
}
