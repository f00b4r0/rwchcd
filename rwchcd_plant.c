//
//  rwchcd_plant.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation implementation.
 * @todo valve PI(D) controller
 * @todo summer run: valve mid position, periodic run of pumps - switchover condition is same as circuit_outhoff with target_temp = preset summer switchover temp
 * @todo plant_save()/plant_restore() (for e.g. dynamically created plants)
 */

#include <stdlib.h>	// calloc/free
#include <unistd.h>	// sleep
#include <math.h>	// roundf
#include <assert.h>

#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_logic.h"
#include "rwchcd_plant.h"

/** PUMP **/

/**
 * Delete a pump
 * Frees all pump-local resources
 * @param pump the pump to delete
 */
static void pump_del(struct s_pump * restrict pump)
{
	if (!pump)
		return;

	hardware_relay_release(pump->set.rid_relay);
	free(pump->name);
	pump->name = NULL;
	free(pump);
}

/**
 * Put pump online.
 * Perform all necessary actions to prepare the pump for service.
 * @param pump target pump
 * @return exec status
 * @warning no parameter check
 */
static int pump_online(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);
	
	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	return (ALL_OK);
}

/**
 * Set pump state.
 * @param pump target pump
 * @param req_on request pump on if true
 * @param force_state skips cooldown if true
 * @return error code if any
 */
static int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state)
{
	assert(pump);
	
	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->run.online)
		return (-EOFFLINE);
	
	pump->run.req_on = req_on;
	pump->run.force_state = force_state;
	
	return (ALL_OK);
}

/**
 * Get pump state.
 * @param pump target pump
 * @return pump state
 */
static int pump_get_state(const struct s_pump * restrict const pump)
{
	assert(pump);
	
	if (!pump->set.configured)
		return (-ENOTCONFIGURED);
	
	// XXX we could return remaining cooldown time if necessary
	return (hardware_relay_get_state(pump->set.rid_relay));
}

/**
 * Put pump offline.
 * Perform all necessary actions to completely shut down the pump.
 * @param pump target pump
 * @return exec status
 * @warning no parameter check
 */
static int pump_offline(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);
	
	if (!pump->set.configured)
		return (-ENOTCONFIGURED);
	
	return(pump_set_state(pump, OFF, FORCE));
}

/**
 * Run pump.
 * @param pump target pump
 * @return exec status
 */
static int pump_run(struct s_pump * restrict const pump)
{
	time_t cooldown = 0;	// by default, no wait
	
	assert(pump);
	
	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (!pump->run.online)
		return (-EOFFLINE);

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!pump->run.req_on && !pump->run.force_state)
		cooldown = pump->run.actual_cooldown_time ? pump->run.actual_cooldown_time : pump->set.cooldown_time;
	
	// this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	pump->run.actual_cooldown_time = hardware_relay_set_state(pump->set.rid_relay, pump->run.req_on, cooldown);

	return (ALL_OK);
}

/** VALVE **/

/**
 * Delete a valve
 * Frees all valve-local resources
 * @param valve the valve to delete
 */
static void valve_del(struct s_valve * valve)
{
	if (!valve)
		return;

	hardware_relay_release(valve->set.rid_open);
	hardware_relay_release(valve->set.rid_close);
	free(valve->priv);
	valve->priv = NULL;
	free(valve->name);
	valve->name = NULL;

	free(valve);
}

/**
 * Request valve closing amount
 * @param valve target valve
 * @param percent amount to close the valve
 * @return exec status
 */
static int valve_reqopen_pct(struct s_valve * const valve, uint_fast8_t percent)
{
	assert(valve);
	
	// if valve is opening, add running time
	if (OPEN == valve->run.request_action)
		valve->run.target_course += percent;
	else {
		valve->run.request_action = OPEN;
		valve->run.target_course = percent;
	}
	
	return (ALL_OK);
}

/**
 * Request valve opening amount.
 * @param valve target valve
 * @param percent amount to open the valve
 * @return exec status
 */
static int valve_reqclose_pct(struct s_valve * const valve, uint_fast8_t percent)
{
	assert(valve);
	
	// if valve is opening, add running time
	if (CLOSE == valve->run.request_action)
		valve->run.target_course += percent;
	else {
		valve->run.request_action = CLOSE;
		valve->run.target_course = percent;
	}
	
	return (ALL_OK);
}

#define valve_reqopen_full(valve)	valve_reqopen_pct(valve, 120)	///< request valve full open
#define valve_reqclose_full(valve)	valve_reqclose_pct(valve, 120)	///< request valve full close

/**
 * Request valve stop
 * @param valve target valve
 * @return exec status
 */
static int valve_reqstop(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	valve->run.request_action = STOP;
	valve->run.target_course = 0;

	return (ALL_OK);
}

#if 0
/**
 - Current Position is an approximation of the valve's position as it relates to a power level (0 - 100%) where 0% is
 fully closed and 100% is fully open.
 - On Time is the amount of time the valve needs to be turned on (either open or close) to eliminate the error be-
 tween the estimated valve position and the desired power level. A positive On Time value indicates the need to
 open the valve while a negative value indicates the need to close the valve. On Time = (Input 1 Value - Current
 Position) / 100 * Valve Travel Time
 When power is applied to the controller, the valve is closed and time is set to 0
 
 * Implement time-based PI controller in velocity form
 * Saturation : max = in1 temp, min = in2 temp
 * We want to output
 http://www.plctalk.net/qanda/showthread.php?t=19141
 // http://www.energieplus-lesite.be/index.php?id=11247
 // http://www.ferdinandpiette.com/blog/2011/08/implementer-un-pid-sans-faire-de-calculs/
 // http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/
 // http://controlguru.com/process-gain-is-the-how-far-variable/
 // http://www.rhaaa.fr/regulation-pid-comment-la-regler-12
 // http://controlguru.com/the-normal-or-standard-pid-algorithm/
 // http://www.csimn.com/CSI_pages/PIDforDummies.html
 // https://en.wikipedia.org/wiki/PID_controller
 */
static int valvelaw_pi(struct s_valve * const valve, const temp_t target_tout)
{
#warning broken
	int_fast16_t percent;
	temp_t tempin1, tempin2, tempout, error;
	temp_t iterm, pterm, output;
	static temp_t prev, output_prev;
	float Kp, Ki;	// XXX PID settings
	int ret;
	
	tempin1 = get_temp(valve->set.id_temp1);
	ret = validate_temp(tempin1);
	if (ALL_OK != ret)
		return (ret);
	
	// get current outpout
	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve->run.in_deadzone = true;
		return (-EDEADZONE);
	}
	
	valve->run.in_deadzone = false;
	
	/* if we don't have a sensor for secondary input, guesstimate it
	 treat the provided id as a delta from valve tempout in Kelvin XXX REVISIT,
	 tempin2 = tempout - delta */
	if (valve->set.id_temp2 == 0) {
		tempin2 = tempout - deltaK_to_temp(30);	// XXX 30K delta by default
	}
	else if (valve->set.id_temp2 < 0) {
		tempin2 = tempout - deltaK_to_temp(-(valve->set.id_temp2)); // XXX will need casting
	}
	else {
		tempin2 = get_temp(valve->set.id_temp2);
		ret = validate_temp(tempin2);
		if (ALL_OK != ret)
			return (ret);
	}
	
	// calculate error (target - actual)
	error = target_tout - tempout;	// error is deltaK * 100 (i.e. internal type delta)
	
	// Integral term (Ki * error)
	iterm = Ki * error;
	
	// Proportional term (Kp * (previous - actual)
	pterm = Kp * (prev - tempout);
	prev = tempout;
	
	output = iterm + pterm + output_prev;
	output_prev = output;
	
	// scale result on valve position from 0 to 100%
	percent = ((output - tempin2)*100 / (tempin1 - tempin2));
	
	dbgmsg("error: %.1f, iterm: %.1f, pterm: %.1f, output: %.1f, percent: %d%%",
	       temp_to_celsius(error), temp_to_celsius(iterm), temp_to_celsius(pterm), temp_to_celsius(output), percent);
	
	// enforce physical limits
	if (percent > 100)
		percent = 100;
	else if (percent < 0)
		percent = 0;
	
	//	valve->target_position = (int_fast8_t)percent;
	
	return (ALL_OK);
}

/**
 * implement a linear law for valve position:
 * t_outpout = percent * t_input1 + (1-percent) * t_input2
 * side effect sets target_position
 * @param valve self
 * @param target_tout target valve output temperature
 * @return exec status
 */
static int valvelaw_linear(struct s_valve * const valve, const temp_t target_tout)
{
#warning broken
	int_fast16_t percent;
	temp_t tempin1, tempin2, tempout, error;
	int ret;

	tempin1 = get_temp(valve->set.id_temp1);
	ret = validate_temp(tempin1);
	if (ALL_OK != ret)
		return (ret);

	// get current outpout
	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve->run.in_deadzone = true;
		return (-EDEADZONE);
	}
	
	valve->run.in_deadzone = false;

	/* if we don't have a sensor for secondary input, guesstimate it
	 treat the provided id as a delta from valve tempout in Kelvin XXX REVISIT,
	 tempin2 = tempout - delta */
	if (valve->set.id_temp2 == 0) {
		tempin2 = tempout - deltaK_to_temp(30);	// XXX 30K delta by default
	}
	else if (valve->set.id_temp2 < 0) {
		tempin2 = tempout - deltaK_to_temp(-(valve->set.id_temp2)); // XXX will need casting
	}
	else {
		tempin2 = get_temp(valve->set.id_temp2);
		ret = validate_temp(tempin2);
		if (ALL_OK != ret)
			return (ret);
	}

	/* Absolute positionning. We don't use actual tempout here.
	 XXX REVIEW. Should help anticipating variations due to changes in inputs 
	 if tempin2 > tempin1 then the valve will close */
	percent = ((target_tout - tempin2)*100 / (tempin1 - tempin2));

	// Add a proportional amount to compensate for drift
	error = target_tout - tempout;	// error is deltaK * 100 (i.e. internal type delta)
	percent += 2*temp_to_deltaK(error);	// XXX HARDCODED we take 2*Kelvin value as a %offset
	
	dbgmsg("target_tout: %.1f, tempout: %.1f, tempin1: %.1f, tempin2: %.1f, percent: %d, error: %.0f",
	       temp_to_celsius(target_tout), temp_to_celsius(tempout), temp_to_celsius(tempin1), temp_to_celsius(tempin2), percent, temp_to_deltaK(error));

	// enforce physical limits
	if (percent > 100)
		percent = 100;
	else if (percent < 0)
		percent = 0;
	
	//	valve->target_position = (int_fast8_t)percent;

	return (ALL_OK);
}
#endif

/**
 * implement a bang-bang law for valve position.
 * If target_tout > current tempout, open the valve, otherwise close it
 * @warning in case of sensor failure, NO ACTION is performed
 * @param valve self
 * @param target_tout target valve output temperature
 * @return exec status
 */
static int valvelaw_bangbang(struct s_valve * const valve, const temp_t target_tout)
{
	int ret;
	temp_t tempout;

	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve->run.in_deadzone = true;
		valve_reqstop(valve);
		return (-EDEADZONE);
	}
	
	valve->run.in_deadzone = false;
	
	if (target_tout > tempout)
		valve_reqopen_full(valve);
	else
		valve_reqclose_full(valve);
	
	return (ALL_OK);
}

/**
 * Successive approximations law.
 * Approximate the target temperature by repeatedly trying to converge toward
 * the set point. Priv structure contains sample interval, last sample time and
 * fixed amount of valve course to apply.
 * @note settings (in particular deadzone, sample time and amount) are crucial
 * to make this work without too many oscillations.
 * @warning in case of sensor failure, NO ACTION is performed
 * @param valve the target valve
 * @param target_tout the target output temperature
 * @return exec status
 */
int valvelaw_sapprox(struct s_valve * const valve, const temp_t target_tout)
{
	struct s_valve_sapprox_priv * restrict const vpriv = valve->priv;
	const time_t now = time(NULL);
	temp_t tempout;
	int ret;
	
	assert(vpriv);
	
	// sample window
	if ((now - vpriv->last_time) < vpriv->set_sample_intvl)
		return (ALL_OK);
	
	vpriv->last_time = now;
	
	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);
	
	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve->run.in_deadzone = true;
		valve_reqstop(valve);
		return (-EDEADZONE);
	}
	
	valve->run.in_deadzone = false;
	
	// every sample window time, check if temp is < or > target
	// if temp is < target - deadzone/2, open valve for fixed amount
	if (tempout < target_tout - valve->set.tdeadzone/2) {
		valve_reqopen_pct(valve, vpriv->set_amount);
	}
	// if temp is > target + deadzone/2, close valve for fixed amount
	else if (tempout > target_tout + valve->set.tdeadzone/2) {
		valve_reqclose_pct(valve, vpriv->set_amount);
	}
	// else stop valve
	else {
		valve_reqstop(valve);
	}
	
	return (ALL_OK);
}

/**
 * sets valve target position from target temperature.
 * @param valve target valve
 * @param target_tout target temperature at output of valve
 * @return exec status
 */
static inline int valve_tposition(struct s_valve * const valve, const temp_t target_tout)
{
	assert(valve);
	assert(valve->set.configured);

	// apply valve law to determine target position
	return (valve->valvelaw(valve, target_tout));
}

/**
 * Put valve online.
 * Perform all necessary actions to prepare the valve for service.
 * @param valve target valve
 * @return exec status
 * @warning no parameter check
 * @note no check on temperature sensors because some valves (e.g. zone valves)
 * do not need a sensor to be operated.
 */
static int valve_online(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);
	
	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	if (!valve->set.ete_time)
		return (-EMISCONFIGURED);

	// return to idle
	valve_reqstop(valve);
	
	return (ALL_OK);
}

/**
 * Put valve offline.
 * Perform all necessary actions to completely shut down the valve.
 * @param valve target valve
 * @return exec status
 * @warning no parameter check
 */
static int valve_offline(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);
	
	if (!valve->set.configured)
		return (-ENOTCONFIGURED);
	
	// close valve
	valve_reqclose_full(valve);

	return (ALL_OK);
}

/**
 * run valve.
 * @param valve target valve
 * @return error status
 * XXX only handles 3-way valve for now
 - Dead Time is the minimum on time that the valve will travel once it is turned on in either the closed or open di-
 rection. Dead Time = Valve Dead Band / 100 * Valve Travel Time.
 */
static int valve_run(struct s_valve * const valve)
{
#define VALVE_MAX_RUNX	3
	const time_t now = time(NULL);
	time_t request_runtime, runtime, deadtime;	// minimum on time that the valve will travel once it is turned on in either direction.
	float percent_time;	// time necessary per percent position change

	assert(valve);
	
	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	if (!valve->run.online)
		return (-EOFFLINE);

	percent_time = valve->set.ete_time/100.0F;
	
	// calc running time from pct
	request_runtime = (percent_time*valve->run.target_course);	// XXX trunc/floor REVISIT?
	
	// prevent endless run
	if (request_runtime > valve->set.ete_time*VALVE_MAX_RUNX)
		request_runtime = valve->set.ete_time*VALVE_MAX_RUNX;
	
	// if we've exceeded request_runtime, request valve stop
	runtime = now - valve->run.running_since;
	if ((STOP != valve->run.actual_action) && (runtime >= request_runtime))
		valve_reqstop(valve);

	// if we have a change of action, update counters
	if (valve->run.request_action != valve->run.actual_action) {
		// update counters
		if (OPEN == valve->run.actual_action) { // valve has been opening till now
			valve->run.acc_close_time = 0;
			valve->run.acc_open_time += runtime;
			valve->run.actual_position += runtime*10/percent_time;
		}
		else if (CLOSE == valve->run.actual_action) {	// valve has been closing till now
			valve->run.acc_open_time = 0;
			valve->run.acc_close_time += runtime;
			valve->run.actual_position -= runtime*10/percent_time;
		}
	}
	
	// apply physical limits
	if (valve->run.actual_position > 1000)
		valve->run.actual_position = 1000;
	else if (valve->run.actual_position < 0)
		valve->run.actual_position = 0;
	
	// check if stop is requested
	if ((STOP == valve->run.request_action)) {
		hardware_relay_set_state(valve->set.rid_open, OFF, 0);
		hardware_relay_set_state(valve->set.rid_close, OFF, 0);
		valve->run.running_since = 0;
		valve->run.actual_action = STOP;
		return (ALL_OK);
	}
	
	// otherwise check that requested runtime is past deadband
	deadtime = percent_time * valve->set.deadband;
	if (request_runtime < deadtime)
		return (-EDEADBAND);

	dbgmsg("req action: %d, action: %d, pos: %.1f%%, req runtime: %ld, running since: %ld, runtime: %ld",
	       valve->run.request_action, valve->run.actual_action, (float)valve->run.actual_position/10.0F, request_runtime, valve->run.running_since, runtime);
	
	// check what is the requested action
	if (OPEN == valve->run.request_action) {
		if (valve->run.acc_open_time >= valve->set.ete_time*VALVE_MAX_RUNX) {
			valve->run.true_pos = true;
			valve->run.acc_open_time = valve->set.ete_time*VALVE_MAX_RUNX;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
		else {
			hardware_relay_set_state(valve->set.rid_close, OFF, 0);	// break before make
			hardware_relay_set_state(valve->set.rid_open, ON, 0);
			if (!valve->run.running_since || (CLOSE == valve->run.actual_action))
				valve->run.running_since = now;
			valve->run.actual_action = OPEN;
		}
	}
	else if (CLOSE == valve->run.request_action) {
		if (valve->run.acc_close_time >= valve->set.ete_time*VALVE_MAX_RUNX) {
			valve->run.true_pos = true;
			valve->run.acc_close_time = valve->set.ete_time*VALVE_MAX_RUNX;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
		else {
			hardware_relay_set_state(valve->set.rid_open, OFF, 0);	// break before make
			hardware_relay_set_state(valve->set.rid_close, ON, 0);
			if (!valve->run.running_since || (OPEN == valve->run.actual_action))
				valve->run.running_since = now;
			valve->run.actual_action = CLOSE;
		}
	}
	
	return (ALL_OK);
}

/**
 * Constructor for bangbang valve control
 * @param valve target valve
 * @return exec status
 */
int valve_make_bangbang(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);
	
	valve->valvelaw = valvelaw_bangbang;
	
	return (ALL_OK);
}

/** 
 * Constructor for sapprox valve control
 * @param valve target valve
 * @return exec status
 */
int valve_make_sapprox(struct s_valve * const valve)
{
	struct s_valve_sapprox_priv * priv = NULL;
	
	if (!valve)
		return (-EINVALID);
	
	// create priv element
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return (-EOOM);
	
	// attach created priv to valve
	valve->priv = priv;
	
	// assign function
	valve->valvelaw = valvelaw_sapprox;
	
	return (ALL_OK);
}


/** SOLAR **/

/**
 * Create a new solar heater
 * @return pointer to the created solar heater
 */
static struct s_solar_heater * solar_new(void)
{
	struct s_solar_heater * const solar = calloc(1, sizeof(struct s_solar_heater));

	return (solar);
}

/**
 * Delete a solar heater
 * Frees all solar-local resources
 * @param solar the solar heater to delete
 */
static void solar_del(struct s_solar_heater * solar)
{
	if (!solar)
		return;

	free(solar->name);
	solar->name = NULL;
	free(solar);
}

/** BOILER **/

/**
 * Create a new boiler
 * @return pointer to the created boiler
 */
static struct s_boiler_priv * boiler_new(void)
{
	struct s_boiler_priv * const boiler = calloc(1, sizeof(struct s_boiler_priv));

	// set some sane defaults
	if (boiler) {
		boiler->set.histeresis = deltaK_to_temp(6);
		boiler->set.limit_tmin = celsius_to_temp(10);
		boiler->set.limit_tmax = celsius_to_temp(90);
		boiler->set.limit_thardmax = celsius_to_temp(100);
		boiler->set.t_freeze = celsius_to_temp(5);
		boiler->set.burner_min_time = 60 * 4;	// 4mn
	}

	return (boiler);
}

/**
 * Delete a boiler
 * Frees all boiler-local resources
 * @param boiler the boiler to delete
 */
static void boiler_hs_del_priv(void * priv)
{
	struct s_boiler_priv * boiler = priv;

	if (!boiler)
		return;

	hardware_relay_release(boiler->set.rid_burner_1);
	hardware_relay_release(boiler->set.rid_burner_2);

	free(boiler);
}

/**
 * Put boiler online.
 * Perform all necessary actions to prepare the boiler for service.
 * @param heat heatsource parent structure
 * @return exec status
 * @warning no parameter check
 */
static int boiler_hs_online(struct s_heatsource * const heat)
{
	const struct s_boiler_priv * const boiler = heat->priv;
	temp_t testtemp;
	int ret;

	if (!boiler)
		return (-EINVALID);

	// check that mandatory sensors are working
	testtemp = get_temp(boiler->set.id_temp);
	ret = validate_temp(testtemp);
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!boiler->set.limit_tmax)
		ret = -EMISCONFIGURED;
	
	// check that hardmax is > tmax (effectively checks that it's set too)
	if (boiler->set.limit_thardmax < boiler->set.limit_tmax)
		ret = -EMISCONFIGURED;

out:

	return (ret);
}

/**
 * Put boiler offline.
 * Perform all necessary actions to completely shut down the boiler.
 * @param heat heatsource parent structure
 * @return exec status
 * @warning no parameter check
 */
static int boiler_hs_offline(struct s_heatsource * const heat)
{
	struct s_boiler_priv * const boiler = heat->priv;

	if (!boiler)
		return (-EINVALID);

	hardware_relay_set_state(boiler->set.rid_burner_1, OFF, 0);
	hardware_relay_set_state(boiler->set.rid_burner_2, OFF, 0);

	if (boiler->loadpump)
		pump_offline(boiler->loadpump);

	return (ALL_OK);
}

/**
 * Safety routine to apply to boiler in case of emergency.
 * @param boiler target boiler
 */
static void boiler_failsafe(struct s_boiler_priv * const boiler)
{
	hardware_relay_set_state(boiler->set.rid_burner_1, OFF, 0);
	hardware_relay_set_state(boiler->set.rid_burner_2, OFF, 0);
	if (boiler->loadpump)
		pump_set_state(boiler->loadpump, ON, FORCE);
}

/**
 * Boiler self-antifreeze protection.
 * This ensures that the temperature of the boiler body cannot go below a set point.
 * @param boiler target boiler
 * @return error status
 */
static int boiler_antifreeze(struct s_boiler_priv * const boiler)
{
	int ret;
	const temp_t boilertemp = get_temp(boiler->set.id_temp);

	ret = validate_temp(boilertemp);
	if (ret)
		return (ret);

	// trip at set.t_freeze point
	if (boilertemp <= boiler->set.t_freeze)
		boiler->run.antifreeze = true;

	// untrip when boiler reaches set.limit_tmin + histeresis/2
	if (boiler->run.antifreeze) {
		if (boilertemp > (boiler->set.limit_tmin + boiler->set.histeresis/2))
			boiler->run.antifreeze = false;
	}

	return (ALL_OK);
}

/**
 * Boiler logic
 * As a special case in the plant, antifreeze takes over all states if the boiler is configured (and online). XXX REVIEW
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler)
 * @todo burner turn-on anticipation
 */
static int boiler_hs_logic(struct s_heatsource * restrict const heat)
{
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t target_temp = RWCHCD_TEMP_NOREQUEST;
	int ret;

	assert(boiler);
	
	// Check if we need antifreeze
	ret = boiler_antifreeze(boiler);
	if (ret) {
		boiler_failsafe(boiler);
		return (ret);
	}

	switch (heat->run.runmode) {
		case RM_OFF:
			break;
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = heat->run.temp_request;
			break;
		case RM_MANUAL:
			target_temp = boiler->set.limit_tmax;	// set max temp to (safely) trigger burner operation
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}
	
	// bypass target_temp if antifreeze is active
	if (boiler->run.antifreeze)
		target_temp = (target_temp < boiler->set.limit_tmin) ? boiler->set.limit_tmin : target_temp;	// max of the two
	
	// enforce limits
	if (RWCHCD_TEMP_NOREQUEST != target_temp) {	// only if we have an actual heat request
		if (target_temp < boiler->set.limit_tmin)
			target_temp = boiler->set.limit_tmin;
		else if (target_temp > boiler->set.limit_tmax)
			target_temp = boiler->set.limit_tmax;
	}
	else {	// we don't have a temp request
		// if IDLE_NEVER, boiler always runs at min temp
		if (IDLE_NEVER == boiler->set.idle_mode)
			target_temp = boiler->set.limit_tmin;
		// if IDLE_FROSTONLY, boiler runs at min temp unless RM_FROSTFREE
		else if ((IDLE_FROSTONLY == boiler->set.idle_mode) && (RM_FROSTFREE != heat->run.runmode))
			target_temp = boiler->set.limit_tmin;
		// in all other cases the boiler will not be issued a heat request and will be stopped if run.could_sleep is set
		else if (!heat->run.could_sleep)
			target_temp = boiler->set.limit_tmin;
		else
			heat->run.runmode = RM_OFF;
	}
	
	boiler->run.target_temp = target_temp;

	return (ALL_OK);
}

/**
 * Implement basic single allure boiler.
 * @note As a special case in the plant, antifreeze takes over all states if the boiler is configured (and online).
 * @note the boiler trip/untrip points are target +/- histeresis/2
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler)
 * @warning no parameter check
 * @todo XXX TODO: implement 2nd allure (p.51)
 * @todo XXX TODO: review consummer inhibit signal formula for cool startup
 * @todo XXX TODO: implement limit on return temp (p.55/56)
 */
static int boiler_hs_run(struct s_heatsource * const heat)
{
	struct s_boiler_priv * restrict const boiler = heat->priv;
	temp_t boiler_temp, trip_temp, untrip_temp;
	int ret;

	assert(boiler);
	
	switch (heat->run.runmode) {
		case RM_OFF:
			if (!boiler->run.antifreeze)
				return (boiler_hs_offline(heat));	// Only if no antifreeze (see above)
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
		case RM_MANUAL:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}
	
	// if we reached this point then the boiler is active (online or antifreeze)

	// Ensure safety first

	// check mandatory sensor
	boiler_temp = get_temp(boiler->set.id_temp);
	ret = validate_temp(boiler_temp);
	if (ALL_OK != ret) {
		boiler_failsafe(boiler);
		return (ret);
	}

	// ensure boiler is within safety limits
	if (boiler_temp > boiler->set.limit_thardmax) {
		boiler_failsafe(boiler);
		heat->run.consumer_shift = RWCHCD_CSHIFT_MAX;
		return (-ESAFETY);
	}
	
	// we're good to go

	dbgmsg("running: %d, target_temp: %.1f, boiler_temp: %.1f", hardware_relay_get_state(boiler->set.rid_burner_1), temp_to_celsius(boiler->run.target_temp), temp_to_celsius(boiler_temp));
	
	// form consumer shift request if necessary
	if (boiler_temp < boiler->set.limit_tmin) {
		// percentage of shift is formed by the difference between current temp and expected temp in K * 10: 1K down means -10% shift
		heat->run.consumer_shift = (boiler_temp - boiler->set.limit_tmin)/10;
	}
		
	// turn pump on if any
	if (boiler->loadpump)
		pump_set_state(boiler->loadpump, ON, 0);
	
	// un/trip points - histeresis/2 (common practice), assuming sensor will always be significantly cooler than actual output
	if (RWCHCD_TEMP_NOREQUEST != boiler->run.target_temp) {	// apply trip_temp only if we have a heat request
		trip_temp = (boiler->run.target_temp - boiler->set.histeresis/2);
		if (trip_temp < boiler->set.limit_tmin)
			trip_temp = boiler->set.limit_tmin;
	}
	else
		trip_temp = 0;
	
	untrip_temp = (boiler->run.target_temp + boiler->set.histeresis/2);
	if (untrip_temp > boiler->set.limit_tmax)
		untrip_temp = boiler->set.limit_tmax;
	
	// burner control - cooldown is applied to both turn-on and turn-off to avoid pumping effect that could damage the burner
	if (boiler_temp < trip_temp)		// trip condition
		hardware_relay_set_state(boiler->set.rid_burner_1, ON, boiler->set.burner_min_time);	// cooldown start
	else if (boiler_temp > untrip_temp)	// untrip condition
		hardware_relay_set_state(boiler->set.rid_burner_1, OFF, boiler->set.burner_min_time);	// delayed stop

	return (ALL_OK);
}

/** HEATSOURCE **/

/**
 * Put heatsource online.
 * Perform all necessary actions to prepare the heatsource for service but
 * DO NOT MARK IT AS ONLINE.
 * @param heat target heatsource
 * @param return exec status
 */
static int heatsource_online(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (NONE == heat->set.type)	// type NONE, nothing to do
		return (ALL_OK);
	
	if (heat->hs_online)
		ret = heat->hs_online(heat);

	return (ret);
}

/**
 * Put heatsource offline.
 * Perform all necessary actions to completely shut down the heatsource but
 * DO NOT MARK IT AS OFFLINE.
 * @param heat target heatsource
 * @param return exec status
 */
static int heatsource_offline(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	heat->run.runmode = RM_OFF;

	if (NONE == heat->set.type)	// type NONE, nothing to do
		return (ALL_OK);
	
	if (heat->hs_offline)
		ret = heat->hs_offline(heat);

	return (ret);
}

/**
 * Run heatsource.
 * @note Honoring SYSMODE is left to private routines
 * @param heat target heatsource
 * @return exec status
 */
static int heatsource_run(struct s_heatsource * const heat)
{
	assert(heat);
	
	if (!heat->set.configured)
		return (-ENOTCONFIGURED);
	
	if (!heat->run.online)
		return (-EOFFLINE);

	if (NONE == heat->set.type)	// type NONE, nothing to do
		return (ALL_OK);
	
	if (heat->hs_run)
		return (heat->hs_run(heat));
	else
		return (-ENOTIMPLEMENTED);
}

/** CIRCUIT **/

/**
 Loi d'eau linaire: pente + offset
 pente calculee negative puisqu'on conserve l'axe des abscisses dans la bonne orientation
 XXX TODO implementer courbure
 https://pompe-a-chaleur.ooreka.fr/astuce/voir/111578/le-regulateur-loi-d-eau-pour-pompe-a-chaleur
 http://www.energieplus-lesite.be/index.php?id=10959
 http://herve.silve.pagesperso-orange.fr/regul.htm
 * @param circuit self
 * @param source_temp outdoor temperature to consider
 * @return a target water temperature for this circuit
 * @warning no parameter check
 */
static temp_t templaw_linear(const struct s_heating_circuit * const circuit, const temp_t source_temp)
{
	const struct s_tlaw_lin20C_priv * tld = circuit->tlaw_data_priv;
	float slope;
	temp_t offset;
	temp_t t_output, curve_shift;
	
	assert(tld);

	// pente = (Y2 - Y1)/(X2 - X1)
	slope = ((float)(tld->twater2 - tld->twater1)) / (tld->tout2 - tld->tout1);
	// offset: reduction par un point connu
	offset = tld->twater2 - (tld->tout2 * slope);

	// calculate output at nominal 20C: Y = input*slope + offset
	t_output = roundf(source_temp * slope) + offset;

	// shift output based on actual target temperature
	curve_shift = (circuit->run.target_ambient - celsius_to_temp(20)) * (1 - slope);
	t_output += curve_shift;

	return (t_output);
}

/**
 * Put circuit online.
 * Perform all necessary actions to prepare the circuit for service but
 * DO NOT MARK IT AS ONLINE.
 * @param circuit target circuit
 * @param return exec status
 */
static int circuit_online(struct s_heating_circuit * const circuit)
{
	temp_t testtemp;
	int ret;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

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
static int circuit_offline(struct s_heating_circuit * const circuit)
{
	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	circuit->run.target_wtemp = 0;
	circuit->run.actual_cooldown_time = 0;

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
 * XXX safety for heating floor if implementing positive consummer_shift()
 * @warning circuit->run.target_ambient must be properly set before this runs
 * @todo review consumer shift formula
 */
static int circuit_run(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	const time_t now = time(NULL);
	temp_t water_temp, curr_temp, lwtmin, lwtmax;
	int ret;

	assert(circuit);
	
	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	if (!circuit->run.online)
		return (-EOFFLINE);
	
	// handle special runmode cases
	switch (circuit->run.runmode) {
		case RM_OFF:
			if (circuit->run.actual_cooldown_time > 0) {	// delay offlining
				// disable heat request from this circuit
				circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				// decrement counter
				circuit->run.actual_cooldown_time -= (now - circuit->run.last_run_time);
				dbgmsg("in cooldown, remaining: %ld", circuit->run.actual_cooldown_time);
				// stop processing: maintain current wtemp
				goto valve;
			}
			else
				return (circuit_offline(circuit));
		case RM_MANUAL:
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
	
	circuit->run.actual_cooldown_time = runtime->consumer_stop_delay;

	// circuit is active, ensure pump is running
	if (circuit->pump)
		pump_set_state(circuit->pump, ON, 0);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, runtime->t_outdoor_mixed);
	
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
	
	if (water_temp < lwtmin)
		water_temp = lwtmin;
	
	// apply global shift - can override lwtmin - XXX FORMULA - XXX will impact heat request
	water_temp += deltaK_to_temp((0.25F * runtime->consumer_shift));
	
	if (water_temp > lwtmax)
		water_temp = lwtmax;

	dbgmsg("request_amb: %.1f, target_amb: %.1f, target_wt: %.1f, curr_wt: %.1f, curr_rwt: %.1f",
	       temp_to_celsius(circuit->run.request_ambient), temp_to_celsius(circuit->run.target_ambient),
	       temp_to_celsius(water_temp), temp_to_celsius(get_temp(circuit->set.id_temp_outgoing)),
	       temp_to_celsius(get_temp(circuit->set.id_temp_return)));
	
	// save current target water temp
	circuit->run.target_wtemp = water_temp;

	// apply heat request: water temp + offset
	circuit->run.heat_request = water_temp + SETorDEF(circuit->set.params.temp_inoffset, runtime->config->def_circuit.temp_inoffset);

valve:
	// adjust valve position if necessary
	if (circuit->valve && circuit->valve->set.configured) {
		ret = valve_tposition(circuit->valve, circuit->run.target_wtemp);
		if (ret && (ret != -EDEADZONE))	// return error code if it's not EDEADZONE
			goto out;
	}

	ret = ALL_OK;
out:
	circuit->run.last_run_time = now;
	return (ret);
}

/**
 * Assign linear temperature law to the circuit.
 * @param circuit target circuit
 * @return error status
 */
int circuit_make_linear(struct s_heating_circuit * const circuit)
{
	struct s_tlaw_lin20C_priv * priv = NULL;
	
	if (!circuit)
		return (-EINVALID);

	// create priv element
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return (-EOOM);
	
	// attach created priv to valve
	circuit->tlaw_data_priv = priv;
	
	circuit->templaw = templaw_linear;

	return (ALL_OK);
}

/** DHWT **/

/**
 * Put dhwt online.
 * Perform all necessary actions to prepare the dhwt for service but
 * DO NOT MARK IT AS ONLINE.
 * @param dhwt target dhwt
 * @param return exec status
 */
static int dhwt_online(struct s_dhw_tank * const dhwt)
{
	temp_t testtemp;
	int ret = -EGENERIC;

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(dhwt->set.id_temp_bottom);
	ret = validate_temp(testtemp);
	if (ALL_OK != ret) {
		testtemp = get_temp(dhwt->set.id_temp_top);
		ret = validate_temp(testtemp);
	}
	if (ret)
		goto out;

out:
	return (ret);
}

/**
 * Put dhwt offline.
 * Perform all necessary actions to completely shut down the dhwt but
 * DO NOT MARK IT AS OFFLINE.
 * @param dhwt target dhwt
 * @param return error status
 */
static int dhwt_offline(struct s_dhw_tank * const dhwt)
{
	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;
	dhwt->run.target_temp = 0;
	dhwt->run.force_on = false;
	dhwt->run.recycle_on = false;
	dhwt->run.legionella_on = false;
	dhwt->run.charge_since = 0;

	if (dhwt->feedpump)
		pump_offline(dhwt->feedpump);

	if (dhwt->recyclepump)
		pump_offline(dhwt->recyclepump);

	if (dhwt->set.rid_selfheater)
		hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);

	dhwt->run.runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * DHWT control loop.
 * Controls the dhwt's elements to achieve the desired target temperature.
 * @param dhwt target dhwt
 * @return error status
 * @warning no failsafe in case of sensor failure
 * @bug pump management for discharge protection needs review
 * @todo XXX TODO: implement dhwprio glissante/absolue for heat request
 * @todo XXX TODO: implement working on electric without sensor
 */
static int dhwt_run(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t water_temp, top_temp, bottom_temp, curr_temp, wintmax;
	bool valid_ttop = false, valid_tbottom = false, test;
	const time_t now = time(NULL);
	time_t limit;
	int ret;

	assert(dhwt);
	
	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	if (!dhwt->run.online)
		return (-EOFFLINE);

	switch (dhwt->run.runmode) {
		case RM_OFF:
			return (dhwt_offline(dhwt));
		case RM_COMFORT:
		case RM_ECO:
		case RM_FROSTFREE:
			break;
		case RM_MANUAL:
			if (dhwt->feedpump)
				pump_set_state(dhwt->feedpump, ON, FORCE);
			if (dhwt->recyclepump)
				pump_set_state(dhwt->recyclepump, ON, FORCE);
			hardware_relay_set_state(dhwt->set.rid_selfheater, ON, 0);
			return (ALL_OK);
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active

	// check which sensors are available
	bottom_temp = get_temp(dhwt->set.id_temp_bottom);
	ret = validate_temp(bottom_temp);
	if (ALL_OK == ret)
		valid_tbottom = true;
	top_temp = get_temp(dhwt->set.id_temp_top);
	ret = validate_temp(top_temp);
	if (ALL_OK == ret)
		valid_ttop = true;

	// no sensor available, give up
	if (!valid_tbottom && !valid_ttop)
		return (ret);	// return last error

	// We're good to go
	
	dbgmsg("charge since: %ld, target_temp: %.1f, bottom_temp: %.1f, top_temp: %.1f",
	       dhwt->run.charge_since, temp_to_celsius(dhwt->run.target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));
	
	// handle recycle loop
	if (dhwt->recyclepump) {
		if (dhwt->run.recycle_on)
			pump_set_state(dhwt->recyclepump, ON, NOFORCE);
		else
			pump_set_state(dhwt->recyclepump, OFF, NOFORCE);
	}
	
	/* handle heat charge - XXX we enforce sensor position, it SEEMS desirable
	   apply histeresis on logic: trip at target - histeresis (preferably on low sensor),
	   untrip at target (preferably on high sensor). */
	if (!dhwt->run.charge_since) {	// no charge in progress
		if (valid_tbottom)	// prefer bottom temp if available
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;
		
		// if charge not in progress, trip if forced or at (target temp - histeresis)
		if (dhwt->run.force_on || (curr_temp < (dhwt->run.target_temp - SETorDEF(dhwt->set.params.histeresis, runtime->config->def_dhwt.histeresis)))) {
			if (runtime->plant_could_sleep && dhwt->set.rid_selfheater) {
				// the plant is sleeping and we have a configured self heater: use it
				hardware_relay_set_state(dhwt->set.rid_selfheater, ON, 0);
			}
			else {	// run from plant heat source
				// calculate necessary water feed temp: target tank temp + offset
				water_temp = dhwt->run.target_temp + SETorDEF(dhwt->set.params.temp_inoffset, runtime->config->def_dhwt.temp_inoffset);

				// enforce limits
				wintmax = SETorDEF(dhwt->set.params.limit_wintmax, runtime->config->def_dhwt.limit_wintmax);
				if (water_temp > wintmax)
					water_temp = wintmax;

				// apply heat request
				dhwt->run.heat_request = water_temp;

				/* feedpump management */

				test = ON;	// by default, turn on pump

				// if available, test for inlet water temp
				water_temp = get_temp(dhwt->set.id_temp_win);	// XXX REVIEW: if this sensor relies on pump running for accurate read, then this can be a problem
				ret = validate_temp(water_temp);
				if (ALL_OK == ret) {
					// discharge protection: if water feed temp is < dhwt current temp, stop the pump
					if (water_temp < curr_temp)	// XXX REVIEW: no histeresis
						test = OFF;
				}

				// turn feedpump on
				if (dhwt->feedpump)
					pump_set_state(dhwt->feedpump, test, NOFORCE);
			}
			
			// mark heating in progress
			dhwt->run.charge_since = now;
		}
	}
	else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
		if (valid_ttop)	// prefer top temp if available
			curr_temp = top_temp;
		else
			curr_temp = bottom_temp;

		// untrip conditions
		test = false;

		// if heating gone overtime, untrip
		limit = SETorDEF(dhwt->set.params.limit_chargetime, runtime->config->def_dhwt.limit_chargetime);
		if ((limit) && ((now - dhwt->run.charge_since) > limit))
			test = true;

		// if heating in progress, untrip at target temp
		if (curr_temp > dhwt->run.target_temp)
			test = true;

		// stop all heat input (ensures they're all off at switchover)
		if (test) {
			// stop self-heater (if any)
			hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);

			/* feedpump management */

			test = FORCE;	// by default, force feedpump immediate turn off

			// if available, test for inlet water temp
			water_temp = get_temp(dhwt->set.id_temp_win);
			ret = validate_temp(water_temp);
			if (ALL_OK == ret) {
				// discharge protection: if water feed temp is > dhwt current temp, we can apply cooldown
				if (water_temp > curr_temp)
					test = NOFORCE;
			}

			// turn off pump with conditional cooldown
			if (dhwt->feedpump)
				pump_set_state(dhwt->feedpump, OFF, test);

			/* end feedpump management */

			// clear heat request
			dhwt->run.heat_request = RWCHCD_TEMP_NOREQUEST;

			// untrip force charge: XXX force can run only once
			dhwt->run.force_on = false;

			// mark heating as done
			dhwt->run.charge_since = 0;
		}
	}

	return (ALL_OK);
}


/** PLANT **/

/**
 * Create a new pump and attach it to the plant.
 * @param plant the plant to attach the pump to
 * @return pointer to the created pump
 */
struct s_pump * plant_new_pump(struct s_plant * const plant)
{
	struct s_pump * restrict pump = NULL;
	struct s_pump_l * restrict pumpelmt = NULL;
	
	if (!plant)
		goto fail;
	
	// create a new pump. calloc() sets good defaults
	pump = calloc(1, sizeof(*pump));
	if (!pump)
		goto fail;
	
	// create pump element
	pumpelmt = calloc(1, sizeof(*pumpelmt));
	if (!pumpelmt)
		goto fail;
	
	// attach created pump to element
	pumpelmt->pump = pump;
	
	// attach it to the plant
	pumpelmt->id = plant->pump_n;
	pumpelmt->next = plant->pump_head;
	plant->pump_head = pumpelmt;
	plant->pump_n++;
	
	return (pump);
	
fail:
	free(pump);
	free(pumpelmt);
	return (NULL);
}

/**
 * Create a new valve and attach it to the plant.
 * @param plant the plant to attach the valve to
 * @return pointer to the created valve
 */
struct s_valve * plant_new_valve(struct s_plant * const plant)
{
	struct s_valve * restrict valve = NULL;
	struct s_valve_l * restrict valveelmt = NULL;
	
	if (!plant)
		goto fail;
	
	// create a new valve. calloc() sets good defaults
	valve = calloc(1, sizeof(*valve));
	if (!valve)
		goto fail;
	
	// create valve element
	valveelmt = calloc(1, sizeof(*valveelmt));
	if (!valveelmt)
		goto fail;
	
	// attach created valve to element
	valveelmt->valve = valve;
	
	// attach it to the plant
	valveelmt->id = plant->valve_n;
	valveelmt->next = plant->valve_head;
	plant->valve_head = valveelmt;
	plant->valve_n++;
	
	return (valve);
	
fail:
	free(valve);
	free(valveelmt);
	return (NULL);
}

/**
 * Create a new heating circuit and attach it to the plant.
 * @param plant the plant to attach the circuit to
 * @return pointer to the created heating circuit
 */
struct s_heating_circuit * plant_new_circuit(struct s_plant * const plant)
{
	struct s_heating_circuit * restrict circuit = NULL;
	struct s_heating_circuit_l * restrict circuitelement = NULL;

	if (!plant)
		goto fail;

	// create a new circuit. calloc() sets good defaults
	circuit = calloc(1, sizeof(*circuit));
	if (!circuit)
		goto fail;

	// create a new circuit element
	circuitelement = calloc(1, sizeof(*circuitelement));
	if (!circuitelement)
		goto fail;

	// attach the created circuit to the element
	circuitelement->circuit = circuit;

	// attach it to the plant
	circuitelement->id = plant->circuit_n;
	circuitelement->next = plant->circuit_head;
	plant->circuit_head = circuitelement;
	plant->circuit_n++;

	return (circuit);

fail:
	free(circuit);
	free(circuitelement);
	return (NULL);
}

/**
 * Circuit destructor.
 * Frees all circuit-local resources
 * @param circuit the circuit to delete
 */
static void circuit_del(struct s_heating_circuit * circuit)
{
	if (!circuit)
		return;

	free(circuit->name);
	circuit->name = NULL;

	free(circuit);
}

/**
 * Create a new dhw tank and attach it to the plant.
 * @param plant the plant to attach the tank to
 * @return pointer to the created tank
 */
struct s_dhw_tank * plant_new_dhwt(struct s_plant * const plant)
{
	struct s_dhw_tank * restrict dhwt = NULL;
	struct s_dhw_tank_l * restrict dhwtelement = NULL;

	if (!plant)
		goto fail;

	// create a new tank. calloc() sets good defaults
	dhwt = calloc(1, sizeof(*dhwt));
	if (!dhwt)
		goto fail;

	// create a new tank element
	dhwtelement = calloc(1, sizeof(*dhwtelement));
	if (!dhwtelement)
		goto fail;

	// attach the created tank to the element
	dhwtelement->dhwt = dhwt;

	// attach it to the plant
	dhwtelement->id = plant->dhwt_n;
	dhwtelement->next = plant->dhwt_head;
	plant->dhwt_head = dhwtelement;
	plant->dhwt_n++;

	return (dhwt);

fail:
	free(dhwt);
	free(dhwtelement);
	return (NULL);
}

/**
 * DHWT destructor
 * Frees all dhwt-local resources
 * @param dhwt the dhwt to delete
 */
static void dhwt_del(struct s_dhw_tank * restrict dhwt)
{
	if (!dhwt)
		return;

	solar_del(dhwt->solar);
	dhwt->solar = NULL;
	hardware_relay_release(dhwt->set.rid_selfheater);
	free(dhwt->name);
	dhwt->name = NULL;

	free(dhwt);
}

/**
 * Create a new heatsource in the plant
 * @param plant the target plant
 * @param type the heatsource type to create
 * @return pointer to the created source
 */
struct s_heatsource * plant_new_heatsource(struct s_plant * const plant, const enum e_heatsource_type type)
{
	struct s_heatsource * restrict source = NULL;
	struct s_heatsource_l * restrict sourceelement = NULL;

	if (!plant)
		return (NULL);

	// create a new source. calloc() sets good defaults
	source = calloc(1, sizeof(*source));
	if (!source)
		return (NULL);

	switch (type) {
		case BOILER:
			source->priv = boiler_new();
			source->hs_online = boiler_hs_online;
			source->hs_offline = boiler_hs_offline;
			source->hs_logic = boiler_hs_logic;
			source->hs_run = boiler_hs_run;
			source->hs_del_priv = boiler_hs_del_priv;
			break;
		case NONE:
		default:
			break;
	}

	// check we have a priv element except for type NONE
	if (!source->priv && (NONE != type))
		goto fail;

	source->set.type = type;

	// create a new source element
	sourceelement = calloc(1, sizeof(*sourceelement));
	if (!sourceelement)
		goto fail;

	// attach the created source to the element
	sourceelement->heats = source;

	// attach it to the plant
	sourceelement->id = plant->heats_n;
	sourceelement->next = plant->heats_head;
	plant->heats_head = sourceelement;
	plant->heats_n++;

	return (source);

fail:
	if (source->hs_del_priv)
		source->hs_del_priv(source->priv);
	free(source);
	return (NULL);
}

/**
 * Delete a heatsource
 * @param source the source to delete
 */
static void heatsource_del(struct s_heatsource * source)
{
	if (!source)
		return;

	if (source->hs_del_priv)
		source->hs_del_priv(source->priv);
	source->priv = NULL;

	free(source->name);
	source->name = NULL;
	
	free(source);
}

/**
 * Create a new plant.
 * @return newly created pointer or NULL if failed
 */
struct s_plant * plant_new(void)
{
	struct s_plant * const plant = calloc(1, sizeof(struct s_plant));

	return (plant);
}

/**
 * Delete a plant.
 * Turn everything off, deallocate all resources and free pointer
 * @param plant the plant to destroy
 */
void plant_del(struct s_plant * plant)
{
	struct s_pump_l * pumpelmt, * pumpnext;
	struct s_valve_l * valveelmt, * valvenext;
	struct s_heating_circuit_l * circuitelement, * circuitlnext;
	struct s_dhw_tank_l * dhwtelement, * dhwtlnext;
	struct s_heatsource_l * sourceelement, * sourcenext;
	
	if (!plant)
		return;

	// clear all registered pumps
	pumpelmt = plant->pump_head;
	while (pumpelmt) {
		pumpnext = pumpelmt->next;
		pump_del(pumpelmt->pump);
		free(pumpelmt);
		plant->pump_n--;
		pumpelmt = pumpnext;
	}
	
	// clear all registered valves
	valveelmt = plant->valve_head;
	while (valveelmt) {
		valvenext = valveelmt->next;
		valve_del(valveelmt->valve);
		free(valveelmt);
		plant->valve_n--;
		valveelmt = valvenext;
	}
	
	// clear all registered circuits
	circuitelement = plant->circuit_head;
	while (circuitelement) {
		circuitlnext = circuitelement->next;
		circuit_del(circuitelement->circuit);
		free(circuitelement);
		plant->circuit_n--;
		circuitelement = circuitlnext;
	}

	// clear all registered dhwt
	dhwtelement = plant->dhwt_head;
	while (dhwtelement) {
		dhwtlnext = dhwtelement->next;
		dhwt_del(dhwtelement->dhwt);
		free(dhwtelement);
		plant->dhwt_n--;
		dhwtelement = dhwtlnext;
	}

	// clear all registered heatsources
	sourceelement = plant->heats_head;
	while (sourceelement) {
		sourcenext = sourceelement->next;
		heatsource_del(sourceelement->heats);
		free(sourceelement);
		plant->heats_n--;
		sourceelement = sourcenext;
	}

	free(plant);
}

/**
 * Bring plant online.
 * @param plant target plant
 * @return exec status (-EGENERIC if any sub call returned an error)
 * @note REQUIRES valid sensor values before being called
 * @todo error handling
 */
int plant_online(struct s_plant * restrict const plant)
{
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	bool suberror = false;
	int ret;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// online the actuators first
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_online(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("pump_online failed, id: %d (%d)", pumpl->id, ret);
			pump_offline(pumpl->pump);
			pumpl->pump->run.online = false;
			suberror = true;
		}
		else
			pumpl->pump->run.online = true;
	}

	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_online(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("valve_online failed, id: %d (%d)", valvel->id, ret);
			valve_offline(valvel->valve);
			valvel->valve->run.online = false;
			suberror = true;
		}
		else
			valvel->valve->run.online = true;
	}
	
	// next deal with the consummers
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_online(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("circuit_online failed, id: %d (%d)", circuitl->id, ret);
			circuit_offline(circuitl->circuit);
			circuitl->circuit->run.online = false;
			suberror = true;
		}
		else
			circuitl->circuit->run.online = true;
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("dhwt_online failed, id: %d (%d)", dhwtl->id, ret);
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->run.online = false;
			suberror = true;
		}
		else
			dhwtl->dhwt->run.online = true;
	}

	// finally online the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_online(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("heatsource_online failed, id: %d (%d)", heatsourcel->id, ret);
			heatsource_offline(heatsourcel->heats);
			heatsourcel->heats->run.online = false;
			suberror = true;
		}
		else
			heatsourcel->heats->run.online = true;
	}

	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}

/**
 * Take plant offline.
 * @param plant target plant
 * @return exec status (-EGENERIC if any sub call returned an error)
 * @todo error handling
 */
int plant_offline(struct s_plant * restrict const plant)
{
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	bool suberror = false;
	int ret;
	
	if (!plant)
		return (-EINVALID);
	
	if (!plant->configured)
		return (-ENOTCONFIGURED);
	
	// offline the consummers first
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_offline(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("circuit_offline failed, id: %d (%d)", circuitl->id, ret);
			suberror = true;
		}
		circuitl->circuit->run.online = false;
	}
	
	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_offline(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("dhwt_offline failed, id: %d (%d)", dhwtl->id, ret);
			suberror = true;
		}
		dhwtl->dhwt->run.online = false;
	}
	
	// next deal with the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_offline(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("heatsource_offline failed, id: %d (%d)", heatsourcel->id, ret);
			suberror = true;
		}
		heatsourcel->heats->run.online = false;
	}
	
	// finally offline the actuators
	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_offline(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("valve_offline failed, id: %d (%d)", valvel->id, ret);
			suberror = true;
		}
		valvel->valve->run.online = false;
	}
	
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_offline(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("pump_offline failed, id: %d (%d)", pumpl->id, ret);
			suberror = true;
		}
		pumpl->pump->run.online = false;
	}
	
	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}

/**
 * Run the plant.
 * This function operates all plant elements in turn by enumerating through each list.
 * @param plant the target plant to run
 * @return exec status (-EGENERIC if any sub call returned an error)
 * @todo separate error handler
 * @todo XXX TODO: currently supports single heat source, all consummers connected to it
 */
int plant_run(struct s_plant * restrict const plant)
{
	struct s_runtime * restrict const runtime = get_runtime();
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	struct s_valve_l * valvel;
	struct s_pump_l * pumpl;
	int ret;
	bool sleeping = false, suberror = false;
	time_t stop_delay = 0;

	assert(plant);
	
	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// run the consummers first so they can set their requested heat input
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = logic_circuit(circuitl->circuit);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = circuit_run(circuitl->circuit);
		
		circuitl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:
				circuit_offline(circuitl->circuit);
			case -EINVALIDMODE:
				circuitl->circuit->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:	// sensor issues are handled by circuit_run()
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("circuit_logic/run failed on %d (%d)", circuitl->id, ret);
				continue;
		}
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = logic_dhwt(dhwtl->dhwt);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = dhwt_run(dhwtl->dhwt);
		
		dhwtl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:
				dhwt_offline(dhwtl->dhwt);
			case -EINVALIDMODE:
				dhwtl->dhwt->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("dhwt_logic/run failed on %d (%d)", dhwtl->id, ret);
				continue;
		}
	}

	// finally run the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = logic_heatsource(heatsourcel->heats);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = heatsource_run(heatsourcel->heats);

		heatsourcel->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:	// offline the source if anything happens
				heatsource_offline(heatsourcel->heats);
			case -ENOTCONFIGURED:
			case -EOFFLINE:
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:
			case -ESAFETY:	// don't do anything, SAFETY procedure handled by logic()/run()
				suberror = true;
				dbgerr("heatsource_logic/run failed on %d (%d)", heatsourcel->id, ret);
				continue;	// no further processing for this source
		}
		
		if (heatsourcel->heats->run.could_sleep)	// if (a) heatsource isn't sleeping then the plant isn't sleeping
			sleeping = heatsourcel->heats->run.could_sleep;
		
		// max stop delay
		stop_delay = (heatsourcel->heats->run.target_consumer_stop_delay > stop_delay) ? heatsourcel->heats->run.target_consumer_stop_delay : stop_delay;
		runtime->consumer_shift = heatsourcel->heats->run.consumer_shift;	// XXX
	}

	// run the valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_run(valvel->valve);

		valvel->status = ret;
		
		switch (ret) {
			case ALL_OK:
			case -EDEADBAND:	// not an error
				break;
			default:	// offline the valve if anything happens
				valve_offline(valvel->valve);
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("valve_run failed on %d (%d)", valvel->id, ret);
				continue;	// no further processing for this valve
		}
	}
	
	// run the pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_run(pumpl->pump);
		
		pumpl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:	// offline the pump if anything happens
				pump_offline(pumpl->pump);
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("pump_run failed on %d (%d)", pumpl->id, ret);
				continue;	// no further processing for this valve
		}
	}
	
	// reflect global sleeping state
	runtime->plant_could_sleep = sleeping;

	// reflect global stop delay
	runtime->consumer_stop_delay = stop_delay;
	
	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}
