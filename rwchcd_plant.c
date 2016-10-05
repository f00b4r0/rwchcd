//
//  rwchcd_plant.c
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

// plant basic operation functions.
// ideally none of these functions should make use of time

#include <stdlib.h>	// calloc/free
#include <unistd.h>	// sleep
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_logic.h"
#include "rwchcd_plant.h"

/** PUMP **/

/**
 * Delete a pump
 * @param pump the pump to delete
 */
static void del_pump(struct s_pump * pump)
{
	if (!pump)
		return;

	hardware_relay_del(pump->relay);
	pump->relay = NULL;
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
static int pump_online(struct s_pump * const pump)
{
	if (!pump)
		return (-EINVALID);
	
	if (!pump->configured)
		return (-ENOTCONFIGURED);

	return (ALL_OK);
}

/**
 * Set pump state.
 * @param pump target pump
 * @param state target pump state
 * @param force_state skips cooldown if true
 * @return error code if any
 */
static int pump_set_state(struct s_pump * const pump, bool state, bool force_state)
{
	time_t cooldown = 0;	// by default, no wait
	
	if (!pump)
		return (-EINVALID);
	
	if (!pump->configured)
		return (-ENOTCONFIGURED);

	if (!pump->online)
		return (-EOFFLINE);
	
	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!state && !force_state)
		cooldown = pump->actual_cooldown_time ? pump->actual_cooldown_time : pump->set_cooldown_time;
	
	// XXX this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	pump->actual_cooldown_time = hardware_relay_set_state(pump->relay, state, cooldown);

	return (ALL_OK);
}

/**
 * Put pump offline.
 * Perform all necessary actions to completely shut down the pump.
 * @param pump target pump
 * @return exec status
 * @warning no parameter check
 */
static inline int pump_offline(struct s_pump * const pump)
{
	if (!pump)
		return (-EINVALID);
	
	return(pump_set_state(pump, OFF, FORCE));
}

/**
 * Get pump state.
 * @param pump target pump
 * @return pump state
 */
static int pump_get_state(const struct s_pump * const pump)
{
	if (!pump)
		return (-EINVALID);
	
	if (!pump->configured)
		return (-ENOTCONFIGURED);
	
	// XXX we could return remaining cooldown time if necessary
	return (hardware_relay_get_state(pump->relay));
}

/** VALVE **/

/**
 * Delete a valve
 * @param valve the valve to delete
 */
static void del_valve(struct s_valve * valve)
{
	if (!valve)
		return;

	hardware_relay_del(valve->open);
	valve->open = NULL;
	hardware_relay_del(valve->close);
	valve->close = NULL;
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
	if (!valve)
		return (-EINVALID);
	
	// if valve is opening, add running time
	if (valve->request_action == OPEN)
		valve->target_course += percent;
	else {
		valve->request_action = OPEN;
		valve->target_course = percent;
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
	if (!valve)
		return (-EINVALID);
	
	// if valve is opening, add running time
	if (valve->request_action == CLOSE)
		valve->target_course += percent;
	else {
		valve->request_action = CLOSE;
		valve->target_course = percent;
	}
	
	return (ALL_OK);
}

#define valve_reqopen_full(valve)	valve_reqopen_pct(valve, 120)
#define valve_reqclose_full(valve)	valve_reqclose_pct(valve, 120)

/**
 * Request valve stop
 * @param valve target valve
 * @return exec status
 */
static int valve_reqstop(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	valve->request_action = STOP;
	valve->target_course = 0;

	return (ALL_OK);
}

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
	
	tempin1 = get_temp(valve->id_temp1);
	ret = validate_temp(tempin1);
	if (ALL_OK != ret)
		return (ret);
	
	// get current outpout
	tempout = get_temp(valve->id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set_tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set_tdeadzone/2))) {
		valve->in_deadzone = true;
		return (-EDEADZONE);
	}
	
	valve->in_deadzone = false;
	
	/* if we don't have a sensor for secondary input, guesstimate it
	 treat the provided id as a delta from valve tempout in Kelvin XXX REVISIT,
	 tempin2 = tempout - delta */
	if (valve->id_temp2 == 0) {
		tempin2 = tempout - deltaK_to_temp(30);	// XXX 30K delta by default
	}
	else if (valve->id_temp2 < 0) {
		tempin2 = tempout - deltaK_to_temp(-(valve->id_temp2)); // XXX will need casting
	}
	else {
		tempin2 = get_temp(valve->id_temp2);
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

	tempin1 = get_temp(valve->id_temp1);
	ret = validate_temp(tempin1);
	if (ALL_OK != ret)
		return (ret);

	// get current outpout
	tempout = get_temp(valve->id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set_tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set_tdeadzone/2))) {
		valve->in_deadzone = true;
		return (-EDEADZONE);
	}
	
	valve->in_deadzone = false;

	/* if we don't have a sensor for secondary input, guesstimate it
	 treat the provided id as a delta from valve tempout in Kelvin XXX REVISIT,
	 tempin2 = tempout - delta */
	if (valve->id_temp2 == 0) {
		tempin2 = tempout - deltaK_to_temp(30);	// XXX 30K delta by default
	}
	else if (valve->id_temp2 < 0) {
		tempin2 = tempout - deltaK_to_temp(-(valve->id_temp2)); // XXX will need casting
	}
	else {
		tempin2 = get_temp(valve->id_temp2);
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

/**
 * implement a bang-bang law for valve position.
 * If target_tout > current tempout, open the valve, otherwise close it
 * @param valve self
 * @param target_tout target valve output temperature
 * @return exec status
 */
static int valvelaw_bangbang(struct s_valve * const valve, const temp_t target_tout)
{
	int ret;
	temp_t tempout;

	tempout = get_temp(valve->id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set_tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set_tdeadzone/2))) {
		valve->in_deadzone = true;
		valve_reqstop(valve);
		return (-EDEADZONE);
	}
	
	valve->in_deadzone = false;
	
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
	
	if (!vpriv)
		return (-EMISCONFIGURED);
	
	// sample window
	if ((now - vpriv->last_time) < vpriv->set_sample_intvl)
		return (ALL_OK);
	
	vpriv->last_time = now;
	
	tempout = get_temp(valve->id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);
	
	// apply deadzone
	if (((tempout - valve->set_tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set_tdeadzone/2))) {
		valve->in_deadzone = true;
		valve_reqstop(valve);
		return (-EDEADZONE);
	}
	
	valve->in_deadzone = false;
	
	// every sample window time, check if temp is < or > target
	// if temp is < target - deadzone/2, open valve for fixed amount
	if (tempout < target_tout - valve->set_tdeadzone/2) {
		valve_reqopen_pct(valve, vpriv->set_amount);
	}
	// if temp is > target + deadzone/2, close valve for fixed amount
	else if (tempout > target_tout + valve->set_tdeadzone/2) {
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
	if (!valve)
		return (-EINVALID);
	
	if (!valve->configured)
		return (-ENOTCONFIGURED);

	if (!valve->open || !valve->close)
		return (-EMISCONFIGURED);

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
	
	if (!valve->configured)
		return (-ENOTCONFIGURED);

	if (!valve->set_ete_time)
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
	
	if (!valve->configured)
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
#define VALVE_MAX_RUNX	5
	const time_t now = time(NULL);
	time_t request_runtime, runtime, deadtime;	// minimum on time that the valve will travel once it is turned on in either direction.
	float percent_time;	// time necessary per percent position change

	if (!valve)
		return (-EINVALID);

	if (!valve->configured)
		return (-ENOTCONFIGURED);

	if (!valve->online)
		return (-EOFFLINE);

	percent_time = valve->set_ete_time/100.0F;
	
	// calc running time from pct
	request_runtime = (percent_time*valve->target_course);	// XXX trunc/floor REVISIT?
	
	// prevent endless run
	if (request_runtime > valve->set_ete_time*VALVE_MAX_RUNX)
		request_runtime = valve->set_ete_time*VALVE_MAX_RUNX;
	
	// check if valve is currently active
	if (STOP != valve->actual_action) {
		runtime = now - valve->running_since;
		
		// if it is and we have stop request or exceeded request runtime, update counters and stop it
		if ((STOP == valve->request_action) || (runtime >= request_runtime)) {
			if (OPEN == valve->actual_action) {
				valve->acc_close_time = 0;
				valve->acc_open_time += runtime;
				valve->actual_position += runtime*10/percent_time;
			}
			else if (CLOSE == valve->actual_action) {
				valve->acc_open_time = 0;
				valve->acc_close_time += runtime;
				valve->actual_position -= runtime*10/percent_time;
			}
			valve_reqstop(valve);
		}
	}
	
	dbgmsg("req action: %d, action: %d, pos: %.1f%%, req runtime: %d, running since: %d, runtime: %d",
	       valve->request_action, valve->actual_action, (float)valve->actual_position/10.0F, request_runtime, valve->running_since, runtime);
	
	// apply physical limits
	if (valve->actual_position > 1000)
		valve->actual_position = 1000;
	else if (valve->actual_position < 0)
		valve->actual_position = 0;
	
	// check if stop is requested
	if ((STOP == valve->request_action)) {
		hardware_relay_set_state(valve->open, OFF, 0);
		hardware_relay_set_state(valve->close, OFF, 0);
		valve->running_since = 0;
		valve->actual_action = STOP;
		return (ALL_OK);
	}
	
	// otherwise check that requested runtime is past deadband
	deadtime = percent_time * valve->set_deadband;
	if (request_runtime < deadtime)
		return (-EDEADBAND);

	// check what is the requested action
	if (OPEN == valve->request_action) {
		if (valve->acc_open_time >= valve->set_ete_time*VALVE_MAX_RUNX) {
			valve->acc_open_time = valve->set_ete_time*VALVE_MAX_RUNX;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
		else {
			hardware_relay_set_state(valve->close, OFF, 0);	// break before make
			hardware_relay_set_state(valve->open, ON, 0);
			if (!valve->running_since)
				valve->running_since = now;
			valve->actual_action = OPEN;
		}
	}
	else if (CLOSE == valve->request_action) {
		if (valve->acc_close_time >= valve->set_ete_time*VALVE_MAX_RUNX) {
			valve->acc_close_time = valve->set_ete_time*VALVE_MAX_RUNX;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
		else {
			hardware_relay_set_state(valve->open, OFF, 0);	// break before make
			hardware_relay_set_state(valve->close, ON, 0);
			if (!valve->running_since)
				valve->running_since = now;
			valve->actual_action = CLOSE;
		}
	}
	
	return (ALL_OK);
}

int valve_make_linear(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	valve->valvelaw = valvelaw_linear;

	return (ALL_OK);
}

int valve_make_bangbang(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);
	
	valve->valvelaw = valvelaw_bangbang;
	
	return (ALL_OK);
}

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
 * @param solar the solar heater to delete
 */
static void solar_del(struct s_solar_heater * solar)
{
	if (!solar)
		return;

	del_pump(solar->pump);
	solar->pump = NULL;
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
		boiler->set_histeresis = deltaK_to_temp(6);
		boiler->limit_tmin = celsius_to_temp(10);
		boiler->limit_tmax = celsius_to_temp(95);
		boiler->set_tfreeze = celsius_to_temp(5);
		boiler->set_burner_min_time = 60 * 4;	// 4mn
	}

	return (boiler);
}

/**
 * Delete a boiler
 * @param boiler the boiler to delete
 */
static void boiler_hs_del_priv(void * priv)
{
	struct s_boiler_priv * boiler = priv;

	if (!boiler)
		return;

	del_pump(boiler->loadpump);
	boiler->loadpump = NULL;
	hardware_relay_del(boiler->burner_1);
	boiler->burner_1 = NULL;
	hardware_relay_del(boiler->burner_2);
	boiler->burner_2 = NULL;

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

	if (!heat->configured)
		return (-ENOTCONFIGURED);

	if (!boiler)
		return (-EINVALID);

	// check that mandatory sensors are working
	testtemp = get_temp(boiler->id_temp);
	ret = validate_temp(testtemp);
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!boiler->limit_tmax)
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

	if (!heat->configured)
		return (-ENOTCONFIGURED);

	if (!boiler)
		return (-EINVALID);

	hardware_relay_set_state(boiler->burner_1, OFF, 0);
	hardware_relay_set_state(boiler->burner_2, OFF, 0);

	if (boiler->loadpump)
		pump_offline(boiler->loadpump);

	return (ALL_OK);
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
	const temp_t boilertemp = get_temp(boiler->id_temp);

	ret = validate_temp(boilertemp);

	if (ret)
		return (ret);

	// trip at set_tfreeze point
	if (boilertemp <= boiler->set_tfreeze)
		boiler->antifreeze = true;

	// untrip at limit_tmin + histeresis/2
	if (boiler->antifreeze) {
		if (boilertemp > (boiler->limit_tmin + boiler->set_histeresis/2))
			boiler->antifreeze = false;
	}

	return (ALL_OK);
}

/**
 * Implement basic single allure boiler.
 * As a special case in the plant, antifreeze takes over all states if the boiler is configured. XXX REVIEW
 * @param heat heatsource parent structure
 * @return exec status. If error action must be taken (e.g. offline boiler) XXX REVISIT
 * @warning no parameter check
 * @todo XXX implmement 2nd allure (p.51)
 * @todo XXX implemment consummer inhibit signal for cool startup
 * @todo XXX implement consummer force signal for overtemp cooldown
 * @todo XXX implement limit on return temp (p.55/56)
 */
static int boiler_hs_run(struct s_heatsource * const heat)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_boiler_priv * const boiler = heat->priv;
	temp_t boiler_temp, target_temp, trip_temp, untrip_temp;
	int ret;

	if (!heat->configured)
		return (-ENOTCONFIGURED);

	if (!boiler)
		return (-EINVALID);

	// Check if we need antifreeze
	ret = boiler_antifreeze(boiler);
	if (ret)
		return (ret);

	if (!boiler->antifreeze) {	// antifreeze takes over offline mode
		if (!heat->online)
			return (-EOFFLINE);	// XXX caller must call boiler_offline() otherwise pump will not stop after antifreeze
	}

	// assess actual runmode
	if (RM_AUTO == heat->set_runmode)
		heat->actual_runmode = runtime->runmode;
	else
		heat->actual_runmode = heat->set_runmode;

	switch (heat->actual_runmode) {
		case RM_OFF:
			if (!boiler->antifreeze)
				return (boiler_hs_offline(heat));	// Only if no antifreeze (see above)
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = heat->temp_request;
			break;
		case RM_MANUAL:
			target_temp = boiler->limit_tmax;	// XXX set max temp to (safely) trigger burner operation
			break;
		case RM_AUTO:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the boiler is active (online or antifreeze)

	if (boiler->loadpump)
		pump_set_state(boiler->loadpump, ON, 0);

	boiler_temp = get_temp(boiler->id_temp);
	ret = validate_temp(boiler_temp);
	if (ret != ALL_OK)
		return (ret);

	// safety checks
	if (boiler_temp > boiler->limit_tmax) {
		hardware_relay_set_state(boiler->burner_1, OFF, 0);
		hardware_relay_set_state(boiler->burner_2, OFF, 0);
		pump_set_state(boiler->loadpump, ON, FORCE);
		return (-ESAFETY);
	}

	// bypass target_temp if antifreeze is active
	if (boiler->antifreeze)
		target_temp = boiler->set_tfreeze;

	// enforce limits
	if (target_temp < boiler->limit_tmin)
		target_temp = boiler->limit_tmin;
	else if (target_temp > boiler->limit_tmax)
		target_temp = boiler->limit_tmax;

	// save current target
	boiler->target_temp = target_temp;

	dbgmsg("running: %d, target_temp: %.1f, boiler_temp: %.1f", boiler->burner_1->is_on, temp_to_celsius(target_temp), temp_to_celsius(boiler_temp));

	// un/trip points
	trip_temp = (target_temp - boiler->set_histeresis/2);
	if (trip_temp < boiler->limit_tmin)
		trip_temp = boiler->limit_tmin;
	untrip_temp = (target_temp + boiler->set_histeresis/2);
	if (untrip_temp > boiler->limit_tmax)
		untrip_temp = boiler->limit_tmax;
	
	// temp control
	if (boiler_temp < trip_temp)		// trip condition
		hardware_relay_set_state(boiler->burner_1, ON, 0);	// immediate start
	else if (boiler_temp > untrip_temp)	// untrip condition
		hardware_relay_set_state(boiler->burner_1, OFF, boiler->set_burner_min_time);	// delayed stop

	// keep track of low requests for sleepover, if set. XXX antifreeze will reset, is that a bad thing?
	if (boiler->set_sleeping_time) {
		// if burner has been OFF for a continuous period longer than sleeping_time, trigger sleeping
		if ((hardware_relay_get_state(boiler->burner_1) == OFF) && (boiler->burner_1->state_time > boiler->set_sleeping_time))
			heat->sleeping = true;
		else
			heat->sleeping = false;
	}
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

	if (heat->hs_offline)
		ret = heat->hs_offline(heat);

	heat->actual_runmode = RM_OFF;

	return (ret);
}

/**
 * XXX currently supports single heat source, all consummers connected to it
 * XXX Honoring SYSMODE and online is left to private routines
 */
static int heatsource_run(struct s_heatsource * const heat)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	temp_t temp, temp_request = 0;

	if (!heat)
		return (-EINVALID);

	if (!heat->configured)
		return (-ENOTCONFIGURED);

	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (circuitl = runtime->plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		temp = circuitl->circuit->heat_request;
		temp_request = temp > temp_request ? temp : temp_request;
	}

	// then dhwt
	for (dhwtl = runtime->plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		temp = dhwtl->dhwt->heat_request;
		temp_request = temp > temp_request ? temp : temp_request;
	}

	// apply result to heat source
	heat->temp_request = temp_request;

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
	const temp_t out_temp1 = circuit->tlaw_data.tout1;
	const temp_t water_temp1 = circuit->tlaw_data.twater1;
	const temp_t out_temp2 = circuit->tlaw_data.tout2;
	const temp_t water_temp2 = circuit->tlaw_data.twater2;
	float slope;
	temp_t offset;
	temp_t t_output, curve_shift;

	// (Y2 - Y1)/(X2 - X1)
	slope = (water_temp2 - water_temp1) / (out_temp2 - out_temp1);
	// reduction par un point connu
	offset = water_temp2 - (out_temp2 * slope);

	// calculate output at nominal 20C: Y = input*slope + offset
	t_output = source_temp * slope + offset;

	// shift output based on actual target temperature
	curve_shift = (circuit->target_ambient - celsius_to_temp(20)) * (1 - slope);
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

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(circuit->id_temp_outgoing);
	ret = validate_temp(testtemp);
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!circuit->limit_wtmax)
		ret = -EMISCONFIGURED;

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

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	circuit->heat_request = 0;
	circuit->target_wtemp = 0;

	if (circuit->pump)
		pump_offline(circuit->pump);

	if (circuit->valve)
		valve_offline(circuit->valve);

	circuit->actual_runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * Circuit control loop.
 * Controls the circuits elements to achieve the desired target temperature.
 * @param circuit target circuit
 * @return exec status
 * XXX ADD rate of rise cap
 * XXX safety for heating floor if implementing positive consummer_shift()
 * @warning circuit->target_ambient must be properly set before this runs
 */
static int circuit_run(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t water_temp;
	int ret;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	if (!circuit->online)
		return (-EOFFLINE);

	// handle special runmode cases
	switch (circuit->actual_runmode) {
		case RM_OFF:
			return (circuit_offline(circuit));
		case RM_MANUAL:
			valve_reqstop(circuit->valve);	// stop valve
			pump_set_state(circuit->pump, ON, FORCE);	// turn pump on
			return (ALL_OK);	//XXX REVISIT
		case RM_COMFORT:
		case RM_ECO:
		case RM_DHWONLY:
		case RM_FROSTFREE:
			break;
		case RM_AUTO:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the circuit is active

	// circuit is active, ensure pump is running
	pump_set_state(circuit->pump, ON, 0);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, runtime->t_outdoor_mixed);
	
	dbgmsg("request_ambient: %.1f, target_ambient: %.1f, target_wtemp: %.1f, curr_wtemp: %.1f",
	       temp_to_celsius(circuit->request_ambient), temp_to_celsius(circuit->target_ambient),
	       temp_to_celsius(water_temp), temp_to_celsius(get_temp(circuit->id_temp_outgoing)));

	// enforce limits
	if (water_temp < circuit->limit_wtmin)
		water_temp = circuit->limit_wtmin;	// XXX indicator for flooring
	else if (water_temp > circuit->limit_wtmax)
		water_temp = circuit->limit_wtmax;	// XXX indicator for ceiling

	// save current target water temp
	circuit->target_wtemp = water_temp;

	// apply heat request: water temp + offset
	circuit->heat_request = water_temp + circuit->set_temp_inoffset;

	// adjust valve position if necessary
	if (circuit->valve && circuit->valve->configured) {
		ret = valve_tposition(circuit->valve, circuit->target_wtemp);
		if (ret && (ret != -EDEADZONE))	// return error code if it's not EDEADZONE
			return (ret);
	}

	return (ALL_OK);
}

/**
 * Assign linear temperature law to the circuit.
 * @param circuit target circuit
 * @return error status
 */
int circuit_make_linear(struct s_heating_circuit * const circuit)
{
	if (!circuit)
		return (-EINVALID);

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

	if (!dhwt->configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(dhwt->id_temp_bottom);
	ret = validate_temp(testtemp);
	if (ALL_OK != ret) {
		testtemp = get_temp(dhwt->id_temp_top);
		ret = validate_temp(testtemp);
	}
	if (ret)
		goto out;

	// check that mandatory settings are set
	if (!dhwt->limit_wintmax || !dhwt->limit_tmax)
		ret = -EMISCONFIGURED;

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

	if (!dhwt->configured)
		return (-ENOTCONFIGURED);

	dhwt->heat_request = 0;
	dhwt->target_temp = 0;
	dhwt->force_on = false;
	dhwt->charge_on = false;
	dhwt->recycle_on = false;

	if (dhwt->feedpump)
		pump_offline(dhwt->feedpump);

	if (dhwt->recyclepump)
		pump_offline(dhwt->recyclepump);

	if (dhwt->selfheater)
		hardware_relay_set_state(dhwt->selfheater, OFF, 0);

	dhwt->actual_runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * DHWT control loop.
 * Controls the dhwt's elements to achieve the desired target temperature.
 * @param dhwt target dhwt
 * @return error status
 * XXX TODO implement dhwprio glissante/absolue for heat request
 * XXX TODO implement working on electric without sensor
 */
static int dhwt_run(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t target_temp, water_temp, top_temp, bottom_temp, curr_temp;
	bool valid_ttop = false, valid_tbottom = false, test;
	int ret = -EGENERIC;

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->configured)
		return (-ENOTCONFIGURED);

	if (!dhwt->online)
		return (-EOFFLINE);

	// depending on dhwt run mode, assess dhwt target temp
	if (RM_AUTO == dhwt->set_runmode)
		dhwt->actual_runmode = runtime->dhwmode;
	else
		dhwt->actual_runmode = dhwt->set_runmode;

	switch (dhwt->actual_runmode) {
		case RM_OFF:
			return (dhwt_offline(dhwt));
		case RM_COMFORT:
			target_temp = dhwt->set_tcomfort;
			break;
		case RM_ECO:
			target_temp = dhwt->set_teco;
			break;
		case RM_FROSTFREE:
			target_temp = dhwt->set_tfrostfree;
			break;
		case RM_MANUAL:
			pump_set_state(dhwt->feedpump, ON, FORCE);
			pump_set_state(dhwt->recyclepump, ON, FORCE);
			hardware_relay_set_state(dhwt->selfheater, ON, 0);
			return (ALL_OK);	//XXX REVISIT
		case RM_AUTO:
		case RM_DHWONLY:
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active

	// handle recycle loop
	if (dhwt->recycle_on)
		pump_set_state(dhwt->recyclepump, ON, NOFORCE);
	else
		pump_set_state(dhwt->recyclepump, OFF, NOFORCE);

	// enforce limits on dhw temp
	if (target_temp < dhwt->limit_tmin)
		target_temp = dhwt->limit_tmin;
	else if (target_temp > dhwt->limit_tmax)
		target_temp = dhwt->limit_tmax;

	// save current target dhw temp
	dhwt->target_temp = target_temp;

	// check which sensors are available
	bottom_temp = get_temp(dhwt->id_temp_bottom);
	ret = validate_temp(bottom_temp);
	if (ALL_OK == ret)
		valid_tbottom = true;
	top_temp = get_temp(dhwt->id_temp_top);
	ret = validate_temp(top_temp);
	if (ALL_OK == ret)
		valid_ttop = true;

	// no sensor available, give up
	if (!valid_tbottom && !valid_ttop)
		return (ret);	// return last error

	dbgmsg("charge: %d, target_temp: %.1f, bottom_temp: %.1f, top_temp: %.1f",
	       dhwt->charge_on, temp_to_celsius(target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));

	/* handle heat charge - XXX we enforce sensor position, it SEEMS desirable
	   apply histeresis on logic: trip at target - histeresis (preferably on low sensor),
	   untrip at target (preferably on high sensor). */
	if (!dhwt->charge_on) {	// heating off
		if (valid_tbottom)	// prefer bottom temp if available
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;

		// if heating not in progress, trip if forced or at (target temp - histeresis)
		if (dhwt->force_on || (curr_temp < (target_temp - dhwt->set_histeresis))) {
			dbgmsg("trip");
			if (runtime->sleeping && dhwt->selfheater && dhwt->selfheater->configured) {
				// the plant is sleeping and we have a configured self heater: use it
				hardware_relay_set_state(dhwt->selfheater, ON, 0);
			}
			else {	// run from plant heat source
				// calculate necessary water feed temp: target tank temp + offset
				water_temp = target_temp + dhwt->set_temp_inoffset;

				// enforce limits
				if (water_temp < dhwt->limit_wintmin)
					water_temp = dhwt->limit_wintmin;
				else if (water_temp > dhwt->limit_wintmax)
					water_temp = dhwt->limit_wintmax;

				// apply heat request
				dhwt->heat_request = water_temp;

				// turn feedpump on
				pump_set_state(dhwt->feedpump, ON, NOFORCE);
			}
			// mark heating in progress
			dhwt->charge_on = true;
		}
	}
	else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
		if (valid_ttop)	// prefer top temp if available
			curr_temp = top_temp;
		else
			curr_temp = bottom_temp;

		// if heating in progress, untrip at target temp: stop all heat input (ensures they're all off at switchover)
		if (curr_temp > target_temp) {
			dbgmsg("untrip");
			// stop self-heater
			hardware_relay_set_state(dhwt->selfheater, OFF, 0);

			test = FORCE;	// by default, force feedpump immediate turn off

			// if available, test for inlet water temp
			water_temp = get_temp(dhwt->id_temp_win);
			ret = validate_temp(water_temp);
			if (ALL_OK == ret) {
				// if water feed temp is > dhwt target_temp, we can apply cooldown
				if (water_temp > dhwt->target_temp)
					test = NOFORCE;
			}

			// turn off pump with cooldown
			pump_set_state(dhwt->feedpump, OFF, test);


			// set heat request to minimum
			dhwt->heat_request = dhwt->limit_wintmin;

			// untrip force charge: XXX force can run only once
			dhwt->force_on = false;

			// mark heating as done
			dhwt->charge_on = false;
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
	pump = calloc(1, sizeof(struct s_pump));
	if (!pump)
		goto fail;
	
	// create pump element
	pumpelmt = calloc(1, sizeof(struct s_pump_l));
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
	valve = calloc(1, sizeof(struct s_valve));
	if (!valve)
		goto fail;
	
	// create valve element
	valveelmt = calloc(1, sizeof(struct s_valve_l));
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
	circuit = calloc(1, sizeof(struct s_heating_circuit));
	if (!circuit)
		goto fail;

	// create a new circuit element
	circuitelement = calloc(1, sizeof(struct s_heating_circuit_l));
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

static void del_circuit(struct s_heating_circuit * circuit)
{
	if (!circuit)
		return;

	del_valve(circuit->valve);
	circuit->valve = NULL;
	del_pump(circuit->pump);
	circuit->pump = NULL;
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
	dhwt = calloc(1, sizeof(struct s_dhw_tank));
	if (!dhwt)
		goto fail;

	// create a new tank element
	dhwtelement = calloc(1, sizeof(struct s_dhw_tank_l));
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

static void del_dhwt(struct s_dhw_tank * restrict dhwt)
{
	if (!dhwt)
		return;

	solar_del(dhwt->solar);
	dhwt->solar = NULL;
	del_pump(dhwt->feedpump);
	dhwt->feedpump = NULL;
	del_pump(dhwt->recyclepump);
	dhwt->recyclepump = NULL;
	hardware_relay_del(dhwt->selfheater);
	dhwt->selfheater = NULL;
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
		goto fail;

	// create a new source. calloc() sets good defaults
	source = calloc(1, sizeof(struct s_heatsource));
	if (!source)
		goto fail;

	switch (type) {
		case BOILER:
			source->priv = boiler_new();
			source->hs_online = boiler_hs_online;
			source->hs_offline = boiler_hs_offline;
			source->hs_run = boiler_hs_run;
			source->hs_del_priv = boiler_hs_del_priv;
			break;
		default:
			break;
	}

	if (!source->priv)
		goto fail;

	source->type = type;

	// create a new source element
	sourceelement = calloc(1, sizeof(struct s_heatsource_l));
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
static void del_heatsource(struct s_heatsource * source)
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

	// clear all registered pumps
	pumpelmt = plant->pump_head;
	while (pumpelmt) {
		pumpnext = pumpelmt->next;
		del_pump(pumpelmt->pump);
		pumpelmt->pump = NULL;
		free(pumpelmt);
		plant->pump_n--;
		pumpelmt = pumpnext;
	}
	
	// clear all registered valves
	valveelmt = plant->valve_head;
	while (valveelmt) {
		valvenext = valveelmt->next;
		del_valve(valveelmt->valve);
		valveelmt->valve = NULL;
		free(valveelmt);
		plant->valve_n--;
		valveelmt = valvenext;
	}
	
	// clear all registered circuits
	circuitelement = plant->circuit_head;
	while (circuitelement) {
		circuitlnext = circuitelement->next;
		del_circuit(circuitelement->circuit);
		circuitelement->circuit = NULL;
		free(circuitelement);
		plant->circuit_n--;
		circuitelement = circuitlnext;
	}

	// clear all registered dhwt
	dhwtelement = plant->dhwt_head;
	while (dhwtelement) {
		dhwtlnext = dhwtelement->next;
		del_dhwt(dhwtelement->dhwt);
		dhwtelement->dhwt = NULL;
		free(dhwtelement);
		plant->dhwt_n--;
		dhwtelement = dhwtlnext;
	}

	// clear all registered heatsources
	sourceelement = plant->heats_head;
	while (sourceelement) {
		sourcenext = sourceelement->next;
		del_heatsource(sourceelement->heats);
		sourceelement->heats = NULL;
		free(sourceelement);
		plant->heats_n--;
		sourceelement = sourcenext;
	}

	free(plant);
}

/**
 * Bring plant online.
 * @param plant target plant
 * @return error status: if any subops fails this will be set
 * @note REQUIRES valid sensor values before being called
 */
int plant_online(struct s_plant * restrict const plant)
{
	struct s_pump_l * restrict pumpl;
	struct s_valve_l * restrict valvel;
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	struct s_heatsource_l * restrict heatsourcel;
	int ret, finalret = ALL_OK;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// online the actuators first
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_online(pumpl->pump);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("pump_online failed, id: %d (%d)", pumpl->id, ret);
			pump_offline(pumpl->pump);
			pumpl->pump->online = false;
			finalret = ret;
		}
		else
			pumpl->pump->online = true;
	}

	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_online(valvel->valve);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("valve_online failed, id: %d (%d)", valvel->id, ret);
			valve_offline(valvel->valve);
			valvel->valve->online = false;
			finalret = ret;
		}
		else
			valvel->valve->online = true;
	}
	
	// next deal with the consummers
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_online(circuitl->circuit);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("circuit_online failed, id: %d (%d)", circuitl->id, ret);
			circuit_offline(circuitl->circuit);
			circuitl->circuit->online = false;
			finalret = ret;
		}
		else
			circuitl->circuit->online = true;
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("dhwt_online failed, id: %d (%d)", dhwtl->id, ret);
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->online = false;
			finalret = ret;
		}
		else
			dhwtl->dhwt->online = true;
	}

	// finally online the heat source
	heatsourcel = plant->heats_head;	// XXX single heat source
	ret = heatsource_online(heatsourcel->heats);
	if (ALL_OK != ret) {
		// XXX error handling
		dbgerr("heatsource_online failed, id: %d (%d)", heatsourcel->id, ret);
		heatsource_offline(heatsourcel->heats);
		heatsourcel->heats->online = false;
		finalret = ret;
	}
	else
		heatsourcel->heats->online = true;

	return (finalret);
}

/**
 XXX reduce valve if boiler too low
 XXX degraded mode (when sensors are disconnected)
 XXX keep sensor history
 XXX keep running state across power loss?
 XXX summer run: valve mid position, periodic run of pumps - switchover condition is same as circuit_outhoff with target_temp = preset summer switchover temp
 XXX error reporting and handling
 */
int plant_run(struct s_plant * restrict const plant)
{
#warning Does not report errors
	struct s_runtime * restrict const runtime = get_runtime();
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	struct s_heatsource_l * restrict heatsourcel;
	struct s_valve_l * restrict valvel;
	int ret;
	bool sleeping = false;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// run the consummers first so they can set their requested heat input
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		logic_circuit(circuitl->circuit);
		ret = circuit_run(circuitl->circuit);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("circuit_run failed: %d", ret);
			circuit_offline(circuitl->circuit);
			circuitl->circuit->online = false;
		}
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_run(dhwtl->dhwt);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("dhwt_run failed: %d", ret);
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->online = false;
		}
	}

	// finally run the heat source
	{
		heatsourcel = plant->heats_head;	// XXX single heat source
		ret = heatsource_run(heatsourcel->heats);
		if (ALL_OK != ret) {
			// XXX error handling
			dbgerr("heatsource_run failed: %d", ret);
			heatsource_offline(heatsourcel->heats);
			heatsourcel->heats->online = false;
		}
		if (heatsourcel->heats->sleeping)	// if (a) heatsource isn't sleeping then the plant isn't sleeping
			sleeping = heatsourcel->heats->sleeping;
	}

	// run the valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_run(valvel->valve);
		if (ALL_OK != ret) {
			if (-EDEADBAND == ret)
				continue;
			// XXX error handling
			dbgerr("valve_run failed: %d", ret);
			valve_offline(valvel->valve);
			valvel->valve->online = false;
		}
	}
	
	// reflect global sleeping state
	runtime->sleeping = sleeping;

	return (ALL_OK);	// XXX
}
