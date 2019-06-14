//
//  hcircuit.c
//  rwchcd
//
//  (C) 2017-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>
#include <string.h>	// memset

#include "hcircuit.h"
#include "hardware.h"
#include "lib.h"
#include "pump.h"
#include "valve.h"
#include "runtime.h"
#include "models.h"
#include "config.h"
#include "log.h"
#include "logic.h"

#define HCIRCUIT_RORH_1HTAU	(3600*TIMEKEEP_SMULT)	///< 1h tau expressed in internal time representation
#define HCIRCUIT_RORH_DT	(10*TIMEKEEP_SMULT)	///< absolute min for 3600s tau is 8s dt, use 10s

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
	temp_t t_output;
	temp_t diffnum, diffden;
	temp_t slopenum, slopeden;

	assert(tld);

	slopenum = tld->twater2 - tld->twater1;
	slopeden = tld->tout2 - tld->tout1;

	// calculate new parameters based on current outdoor temperature (select adequate segment)
	if (source_temp < tld->toutinfl) {
		diffnum = tld->twaterinfl - tld->twater1;
		diffden = tld->toutinfl - tld->tout1;
	}
	else {
		diffnum = tld->twater2 - tld->twaterinfl;
		diffden = tld->tout2 - tld->toutinfl;
	}

	// calculate output at nominal 20C: Y = input*slope + offset

	// XXX under "normal" conditions, the following operations should not overflow
	t_output = (source_temp - tld->toutinfl) * diffnum;
	t_output /= diffden;		// no rounding: will slightly over estimate output, which is desirable
	t_output += tld->twaterinfl;

	// shift output based on actual target temperature: (tgt - 20C) * (1 - tld->slope)
	t_output += (circuit->run.target_ambient - celsius_to_temp(20)) * (slopeden - slopenum)/slopeden;;

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
	if (temp <= celsius_to_temp(0)) {
		pr_err(_("\"%s\": limit_wtmax must be locally or globally > 0°C"), circuit->name);
		ret = -EMISCONFIGURED;
	}

	// make sure associated building model is configured
	if (!circuit->bmodel || !circuit->bmodel->set.configured) {
		pr_err(_("\"%s\": building model \"%s\" is set but not configured"), circuit->name, circuit->bmodel->name);
		ret = -EMISCONFIGURED;
	}
	// if pump exists check it's correctly configured
	if (circuit->pump_feed && !circuit->pump_feed->set.configured) {
		pr_err(_("\"%s\": pump_feed \"%s\" is set but not configured"), circuit->name, circuit->pump_feed->name);
		ret = -EMISCONFIGURED;
	}

	if (circuit->set.wtemp_rorh) {
		// if ror is requested and valve is not available report misconfiguration
		if (!circuit->valve_mix) {
			pr_err(_("\"%s\": rate of rise control requested but no mixing valve is available"), circuit->name);
			ret = -EMISCONFIGURED;
		}
		// setup rate limiter
		circuit->run.rorh_temp_increment = temp_expw_mavg(0, circuit->set.wtemp_rorh, HCIRCUIT_RORH_1HTAU, HCIRCUIT_RORH_DT);
	}

	// log registration shouldn't cause onlining to fail
	if (hcircuit_log_register(circuit) != ALL_OK)
		pr_err(_("\"%s\": couldn't register for logging"), circuit->name);

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

	if (!circuit->run.active)
		return (ALL_OK);

	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	circuit->run.target_wtemp = 0;
	circuit->run.rorh_update_time = 0;

	if (circuit->pump_feed)
		pump_shutdown(circuit->pump_feed);

	if (circuit->valve_mix)
		valve_shutdown(circuit->valve_mix);

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
	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	valve_reqclose_full(circuit->valve_mix);
	if (circuit->pump_feed)
		(void)pump_set_state(circuit->pump_feed, ON, FORCE);
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
	const timekeep_t now = timekeep_now();
	temp_t water_temp, curr_temp, ret_temp, lwtmin, lwtmax, temp;
	int ret;

	assert(runtime);

	if (!circuit)
		return (-EINVALID);

	if (!circuit->run.online)	// implies set.configured == true
		return (-EOFFLINE);

	ret = logic_hcircuit(circuit);
	if (ALL_OK != ret)
		return (ret);

	// safety checks
	ret = hardware_sensor_clone_temp(circuit->set.tid_outgoing, &curr_temp);
	if (ALL_OK != ret) {
		hcircuit_failsafe(circuit);
		return (ret);
	}

	// we're good to go - keep updating actual_wtemp when circuit is off
	circuit->run.actual_wtemp = curr_temp;

	// force circuit ON during hs_overtemp condition
	if (circuit->pdata->hs_overtemp)
		circuit->run.runmode = RM_COMFORT;

	// handle special runmode cases
	switch (circuit->run.runmode) {
		case RM_OFF:
			if (circuit->run.target_wtemp && (circuit->pdata->consumer_sdelay > 0)) {
				// disable heat request from this circuit
				circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				dbgmsg("\"%s\": in cooldown, remaining: %ld", circuit->name, timekeep_tk_to_sec(circuit->pdata->consumer_sdelay));
				return (ALL_OK);	// stop processing: maintain current output
			}
			else
				return (hcircuit_shutdown(circuit));
		case RM_TEST:
			circuit->run.active = true;
			valve_reqstop(circuit->valve_mix);
			if (circuit->pump_feed)
				(void)pump_set_state(circuit->pump_feed, ON, FORCE);
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
	circuit->run.active = true;

	// if building model isn't online, failsafe
	if (!circuit->bmodel->run.online) {
		hcircuit_failsafe(circuit);
		return (-ESAFETY);
	}

	// circuit is active, ensure pump is running
	if (circuit->pump_feed) {
		ret = pump_set_state(circuit->pump_feed, ON, 0);
		if (ALL_OK != ret) {
			dbgerr("\"%s\": failed to set pump_feed \"%s\" ON (%d)", circuit->name, circuit->pump_feed->name, ret);
			hcircuit_failsafe(circuit);
			return (ret);	// critical error: stop there
		}
	}

	// fetch limits
	lwtmin = SETorDEF(circuit->set.params.limit_wtmin, runtime->config->def_hcircuit.limit_wtmin);
	lwtmax = SETorDEF(circuit->set.params.limit_wtmax, runtime->config->def_hcircuit.limit_wtmax);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, circuit->bmodel->run.t_out_mix);

	// enforce limits

	if (water_temp < lwtmin)
		water_temp = lwtmin;
	else if (water_temp > lwtmax)
		water_temp = lwtmax;

	// save "non-interfered" target water temp, i.e. the real target (within enforced limits)
	circuit->run.target_wtemp = water_temp;

	// heat request is always computed based on non-interfered water_temp value
	circuit->run.heat_request = circuit->run.target_wtemp + SETorDEF(circuit->set.params.temp_inoffset, runtime->config->def_hcircuit.temp_inoffset);

	// alterations to the computed value only make sense if a mixing valve is available
	if (circuit->valve_mix) {
		// interference: apply rate of rise limitation if any: update temp every minute
		// applied first so it's not impacted by the next interferences (in particular power shift). XXX REVIEW: might be needed to move after if ror control is desired on cshift rising edges
		if (circuit->set.wtemp_rorh) {
			// first sample: init target to current temp and set water_temp to current
			if (!circuit->run.rorh_update_time) {
				water_temp = curr_temp;
				circuit->run.rorh_last_target = curr_temp;	// update last_target to current point
				circuit->run.rorh_update_time = now + timekeep_sec_to_tk(60);	// send update_time 60s ahead for low point settling (see below). XXX hardcoded
			}
			// at circuit startup (pump was previously off) let the water settle to lowest point, which we'll use as reference once it's reached.
			else if ((now - circuit->run.rorh_update_time) < 0) {
				water_temp = curr_temp;
				if (curr_temp < circuit->run.rorh_last_target)
					circuit->run.rorh_last_target = curr_temp;
				// if the heat source has not yet reached optimal output, wait before resuming normal algorithm operation
				if (circuit->pdata->consumer_shift < 0)
					circuit->run.rorh_update_time = now + timekeep_sec_to_tk(30);
			}
			// startup is done.
			// Request for temp lower than (or equal) current: don't touch water_temp (let low request pass), update target to current
			else if (water_temp <= curr_temp) {
				circuit->run.rorh_last_target = curr_temp;	// update last_target to current point
				circuit->run.rorh_update_time = now;
			}
			// else: request for higher temp: apply rate limiter
			else {
				if ((now - circuit->run.rorh_update_time) >= HCIRCUIT_RORH_DT) {
					// compute next target step
					temp = circuit->run.rorh_last_target + circuit->run.rorh_temp_increment;
					// new request is min of next target step and actual request
					circuit->run.rorh_last_target = (temp < water_temp) ? temp : water_temp;
					circuit->run.rorh_update_time = now;
				}
				water_temp = circuit->run.rorh_last_target;	// apply current step
			}
		}

		// interference: handle output flooring requests: maintain current or higher wtemp
		if (circuit->run.floor_output)
			water_temp = (water_temp > curr_temp) ? water_temp : curr_temp;

		// interference: apply global power shift
		if (circuit->pdata->consumer_shift) {
			ret = hardware_sensor_clone_temp(circuit->set.tid_return, &ret_temp);
			// if we don't have a return temp or if the return temp is higher than the outgoing temp, use 0°C (absolute physical minimum) as reference
			if ((ALL_OK != ret) || (ret_temp >= water_temp))
				ret_temp = celsius_to_temp(0);

			// X% shift is (current + X*(current - ref)/100). ref is return temp
			water_temp += circuit->pdata->consumer_shift * (water_temp - ret_temp) / 100;
		}

		// enforce maximum temp during overtemp condition
		if (circuit->pdata->hs_overtemp)
			water_temp = lwtmax;

		// low limit can be overriden by external interferences
		// but high limit can never be overriden: re-enact it
		if (water_temp > lwtmax)
			water_temp = lwtmax;

		// adjust valve position if necessary
		ret = valve_mix_tcontrol(circuit->valve_mix, water_temp);
		if (ret && (ret != -EDEADZONE))	// return error code if it's not EDEADZONE
			return (ret);
		// if we want to add a check for nominal power reached: if ((-EDEADZONE == ret) || (get_temp(circuit->set.tid_outgoing) > circuit->run.target_ambient))
	}

#ifdef DEBUG
	(void)hardware_sensor_clone_temp(circuit->set.tid_return, &ret_temp);
	dbgmsg("\"%s\": rq_amb: %.1f, tg_amb: %.1f, tg_wt: %.1f, tg_wt_mod: %.1f, cr_wt: %.1f, cr_rwt: %.1f", circuit->name,
	       temp_to_celsius(circuit->run.request_ambient), temp_to_celsius(circuit->run.target_ambient),
	       temp_to_celsius(circuit->run.target_wtemp), temp_to_celsius(water_temp), temp_to_celsius(curr_temp), temp_to_celsius(ret_temp));
#endif

	return (ALL_OK);
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
			  temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, int_fast16_t nH100)
{
	struct s_tlaw_bilin20C_priv * priv = NULL;
	temp_t toutw20C, offset, tlin;
	float slope;

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
	slope = ((float)(priv->twater2 - priv->twater1)) / (priv->tout2 - priv->tout1);
	// offset: reduce through a known point
	offset = priv->twater2 - (priv->tout2 * slope);

	if (!priv->toutinfl) {
		// calculate outdoor temp for 20C water temp
		toutw20C = ((float)(celsius_to_temp(20) - offset)) / slope;

		// calculate outdoor temp for inflexion point (toutw20C - (30% of toutw20C - tout1))
		priv->toutinfl = toutw20C - ((toutw20C - priv->tout1) * 30 / 100);

		// calculate corrected water temp at inflexion point (tlinear[nH=1] - 20C) * (nH - 1)
		tlin = (priv->toutinfl * slope) + offset;
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
