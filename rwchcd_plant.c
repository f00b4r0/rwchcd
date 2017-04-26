//
//  rwchcd_plant.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation implementation.
 * @todo valve PI(D) controller
 * @todo plant_save()/plant_restore() (for e.g. dynamically created plants)
 * @todo multiple heatsources: in switchover mode (e.g. wood furnace + fuel:
 * switch to fuel when wood dies out) and cascade mode (for large systems).
 * In this context, a "plant" should logically be a collection of consummers
 * and heatsources all connected to each other: in a plant, all the heatsources
 * are providing heat to all of the plant's consummers.
 * @todo critical/non-critical inhibit signals. DHWT inhibit
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

/*- PUMP -*/

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

/*- VALVE -*/

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

/**
 * Request valve closing/opening amount
 * @param valve target valve
 * @param percent amount to open (positive) or close (negative) the valve
 * @return exec status. If requested amount is < valve deadband no action is performed, -EDEADBAND is returned.
 */
static int valve_request_pct(struct s_valve * const valve, int_fast16_t percent)
{
	assert(valve);

	if (abs(percent) < valve->set.deadband)
		return (-EDEADBAND);

	percent *= 10;

	valve->run.request_action = (percent < 0) ? CLOSE : OPEN;
	valve->run.target_course = abs(percent);

	return (ALL_OK);
}

#define valve_reqopen_full(valve)	valve_request_pct(valve, 120)	///< request valve full open
#define valve_reqclose_full(valve)	valve_request_pct(valve, -120)	///< request valve full close

/**
 * Implement time-based PI controller in velocity form.
 * We are driving an integrating actuator, so we want to compute a change in output,
 * not the actual output.
 * Refer to inline comments for implementation details.
 *
 * Mandatory reading:
 * - http://controlguru.com/integral-reset-windup-jacketing-logic-and-the-velocity-pi-form/
 * - http://controlguru.com/pi-control-of-the-heat-exchanger/
 * - http://www.controleng.com/single-article/the-velocity-of-pid/0733c0b7bfa474fb659b259808ddc869.html
 * - https://www.taco-hvac.com/uploads/FileLibrary/app-note-Kp-Ki-100.pdf
 *
 * Further reading:
 * - http://www.plctalk.net/qanda/showthread.php?t=19141
 * - http://www.energieplus-lesite.be/index.php?id=11247
 * - http://www.ferdinandpiette.com/blog/2011/08/implementer-un-pid-sans-faire-de-calculs/
 * - http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/
 * - http://controlguru.com/process-gain-is-the-how-far-variable/
 * - http://www.csimn.com/CSI_pages/PIDforDummies.html
 * - https://en.wikipedia.org/wiki/PID_controller
 * - http://blog.opticontrols.com/archives/344
 *
 * @note we're dealing with two constraints: the PI controller reacts to an observed
 * response to an action, but the problem is that the steps of that action are
 * of fixed size when dealing with a valve actuator (due to deadband and to
 * limit actuator wear).
 * Furthermore, the action itself isn't instantaneous: contrary to e.g. a PWM
 * output, the valve motor has a finite speed: there's a lag between the control
 * change and the moment when that change is fully effected.
 * Therefore, the PI controller will spend a good deal of
 * time reacting to an observed response that doesn't match its required action.
 */
static int valvectrl_pi(struct s_valve * const valve, const temp_t target_tout)
{
	struct s_valve_pi_priv * restrict const vpriv = valve->priv;
	const time_t now = time(NULL);
	int_fast16_t percent;
	temp_t tempin_h, tempin_l, tempout, error, K;
	float iterm, pterm, output;
	float Kp, Ki;
	const time_t dt = now - vpriv->run.last_time;
	int ret;
	time_t Ti;

	assert(vpriv);

	// sample window
	if (dt < vpriv->set.sample_intvl)
		return (ALL_OK);

	vpriv->run.last_time = now;

	// get current outpout
	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		vpriv->run.reset = true;
		return (-EDEADZONE);
	}

	// get current high input
	tempin_h = get_temp(valve->set.id_temp1);
	ret = validate_temp(tempin_h);
	if (ALL_OK != ret)
		return (ret);

	// if we don't have a sensor for low input, guesstimate it
	tempin_l = get_temp(valve->set.id_temp2);
	ret = validate_temp(tempin_l);
	if (ALL_OK != ret)
		tempin_l = tempin_h - vpriv->set.Ksmax;

	// jacketing for saturation
	if (target_tout <= tempin_l) {
		valve_reqclose_full(valve);
		vpriv->run.reset = true;
		return (ALL_OK);
	} else if (target_tout >= tempin_h) {
		valve_reqopen_full(valve);
		vpriv->run.reset = true;
		return (ALL_OK);
	}

	// handle algorithm reset
	if (vpriv->run.reset) {
		vpriv->run.prev_out = tempout;
		vpriv->run.db_acc = 0;
		vpriv->run.reset = false;
		return (ALL_OK);	// skip until next iteration
	}

	/* 
	 (tempin_h - tempin_l)/100 is the process gain K:
	 maximum output delta (Ksmax) / maximum control delta (100%).
	 in fact, this could be scaled over a different law to better control
	 non-linear valves, since this computation implicitely assumes the valve
	 is linear.
	 Kp = 1/K * (Tu/(Td+Tc), with Tc is closed-loop time constant: max(A*Tu,B*Td);
	 with [A,B] in [0.1,0.8],[1,8],[10,80] for respectively aggressive, moderate and conservative tunings.
	 Ki = Kp/Ti with Ti integration time. Ti = Tu
	 */
	K = abs(tempin_h - tempin_l)/100;	// abs() because _h may occasionally be < _l
	Kp = vpriv->run.Kp_u/K;
	Ti = vpriv->set.Tu;
	Ki = Kp/Ti;

	dbgmsg("K: %d, Tc: %ld, Kp: %e, Ki: %e", K, vpriv->run.Tc, Kp, Ki);

	// calculate error: (target - actual)
	error = target_tout - tempout;
	
	// Integral term: (Ki * error) * sample interval
	iterm = Ki * error * dt;
	
	// Proportional term applied to output: Kp * (previous - actual)
	pterm = Kp * (vpriv->run.prev_out - tempout);

	/*
	 Applying the proportional term to the output avoids kicks when
	 setpoint is changed, however it will also "fight back" against
	 such a change. This negative action will eventually be overcome
	 by the integral term.
	 The benefit of this system is that the algorithm cannot windup
	 and setpoint change does not require specific treatment.
	 */

	output = iterm + pterm;

	dbgmsg("error: %d, iterm: %f, pterm: %f, output: %f, acc: %f, pctfl: %f",
	       error, iterm, pterm, output, vpriv->run.db_acc, output + vpriv->run.db_acc);

	percent = floorf(output + vpriv->run.db_acc);

	/*
	 if we are below valve deadband, everything behaves as if the sample rate
	 were reduced: we accumulate the iterm and we don't update the previous
	 tempout. The next time the algorithm is run, everything will be as if
	 it was run with dt = dt_prev + dt. And so on, until the requested change
	 is large enough to trigger an action, at which point the cycle starts again.
	 In essence, this implements a variable sample rate where the algorithm
	 slows down when the variations are limited, which is mathematically acceptable
	 since this is also a point where the internal frequency is much lower
	 and so Nyquist is still satisfied
	 */
	if (valve_request_pct(valve, percent) != ALL_OK)
		vpriv->run.db_acc += iterm;
	else {
		vpriv->run.prev_out = tempout;
		vpriv->run.db_acc = 0;
	}

	return (ALL_OK);
}

/**
 * implement a bang-bang controller for valve position.
 * If target_tout > current tempout, open the valve, otherwise close it
 * @warning in case of sensor failure, NO ACTION is performed
 * @param valve self
 * @param target_tout target valve output temperature
 * @return exec status
 */
static int valvectrl_bangbang(struct s_valve * const valve, const temp_t target_tout)
{
	int ret;
	temp_t tempout;

	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve_reqstop(valve);
		return (-EDEADZONE);
	}

	if (target_tout > tempout)
		valve_reqopen_full(valve);
	else
		valve_reqclose_full(valve);
	
	return (ALL_OK);
}

/**
 * Successive approximations controller.
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
int valvectrl_sapprox(struct s_valve * const valve, const temp_t target_tout)
{
	struct s_valve_sapprox_priv * restrict const vpriv = valve->priv;
	const time_t now = time(NULL);
	temp_t tempout;
	int ret;
	
	assert(vpriv);
	
	// sample window
	if ((now - vpriv->run.last_time) < vpriv->set.sample_intvl)
		return (ALL_OK);
	
	vpriv->run.last_time = now;
	
	tempout = get_temp(valve->set.id_tempout);
	ret = validate_temp(tempout);
	if (ALL_OK != ret)
		return (ret);
	
	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2)))
		return (-EDEADZONE);

	// every sample window time, check if temp is < or > target
	// if temp is < target - deadzone/2, open valve for fixed amount
	if (tempout < target_tout - valve->set.tdeadzone/2) {
		valve_request_pct(valve, vpriv->set.amount);
	}
	// if temp is > target + deadzone/2, close valve for fixed amount
	else if (tempout > target_tout + valve->set.tdeadzone/2) {
		valve_request_pct(valve, -vpriv->set.amount);
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
	return (valve->valvectrl(valve, target_tout));
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

#define VALVE_MAX_RUNX	3
/**
 * Valve logic.
 * Ensures the valve cannot run forever in one direction.
 * Flags when the valve has reached either end at least once.
 * @param valve the target valve
 * @return exec status
 */
static int logic_valve(struct s_valve * const valve)
{
	assert(valve);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	if (!valve->run.online)
		return (-EOFFLINE);
	
	if (OPEN == valve->run.request_action) {
		if (valve->run.acc_open_time >= valve->set.ete_time*VALVE_MAX_RUNX) {
			valve->run.true_pos = true;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
	}
	else if (CLOSE == valve->run.request_action) {
		if (valve->run.acc_close_time >= valve->set.ete_time*VALVE_MAX_RUNX) {
			valve->run.true_pos = true;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
	}

	return (ALL_OK);
}

/**
 * Valve control loop.
 * Triggers the relays based on requested valve operation, and performs time
 * accounting to keep track of how far the valve has travelled.
 * By design, the implementation will overshoot the target position if it cannot
 * be reached due to time resolution.
 * @param valve target valve
 * @return error status
 * @todo XXX REVIEW only handles 3-way valve for now
 * @warning first invocation must be with valve stopped (run.actual_action == STOP),
 * otherwise dt will be out of whack.
 * @note the function assumes that the sanity of the valve argument will be checked before invocation.
 * @warning beware of the resolution limit on valve end-to-end time
 * @warning REVIEW: overshoots
 */
static int valve_run(struct s_valve * const valve)
{
	const time_t now = time(NULL);
	const time_t dt = now - valve->run.last_run_time;
	float percent_time;	// time necessary per percent position change
	int_fast16_t course;
	int ret = ALL_OK;

	valve->run.last_run_time = now;
	
	percent_time = valve->set.ete_time/100.0F;
	assert(percent_time > 0);
	course = dt*10 / percent_time;	// XXX trunc/floor REVISIT?

	// update counters
	switch (valve->run.actual_action) {
		case OPEN:	// valve has been opening till now
			valve->run.acc_close_time = 0;
			valve->run.acc_open_time += dt;
			valve->run.actual_position += course;
			valve->run.target_course -= course;
			break;
		case CLOSE:	// valve has been closing till now
			valve->run.acc_open_time = 0;
			valve->run.acc_close_time += dt;
			valve->run.actual_position -= course;
			valve->run.target_course -= course;
			break;
		case STOP:
		default:
			break;
	}

	// apply physical limits
	if (valve->run.actual_position > 1000)
		valve->run.actual_position = 1000;
	else if (valve->run.actual_position < 0)
		valve->run.actual_position = 0;

	/* valve stop strategy:
	 valve is stopped if next run would overshoot by more than half of the
	 course resolution. */
	if (valve->run.target_course < (course/2))	// residual value is under/overshoot amount
		valve_reqstop(valve);

	// perform requested action
	if (valve->run.request_action != valve->run.actual_action) {
		switch (valve->run.request_action) {
			case OPEN:
				hardware_relay_set_state(valve->set.rid_close, OFF, 0);	// break before make
				hardware_relay_set_state(valve->set.rid_open, ON, 0);
				valve->run.actual_action = OPEN;
				break;
			case CLOSE:
				hardware_relay_set_state(valve->set.rid_open, OFF, 0);	// break before make
				hardware_relay_set_state(valve->set.rid_close, ON, 0);
				valve->run.actual_action = CLOSE;
				break;
			default:
				ret = -EINVALID;
			case STOP:
				hardware_relay_set_state(valve->set.rid_open, OFF, 0);
				hardware_relay_set_state(valve->set.rid_close, OFF, 0);
				valve->run.actual_action = STOP;
				break;
		}
	}

	dbgmsg("req action: %d, action: %d, pos: %.1f%%, req course: %.1f%%",
	       valve->run.request_action, valve->run.actual_action, (float)valve->run.actual_position/10.0F,
	       (float)valve->run.target_course/10.0F);

	return (ret);
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
	
	valve->valvectrl = valvectrl_bangbang;
	
	return (ALL_OK);
}

/** 
 * Constructor for sapprox valve control
 * @param valve target valve
 * @param amount movement amount in %
 * @param intvl sample interval in seconds
 * @return exec status
 * @warning should ensure that the sample interval allows full amount movement
 */
int valve_make_sapprox(struct s_valve * const valve,
		       uint_fast8_t amount, time_t intvl)
{
	struct s_valve_sapprox_priv * priv = NULL;
	
	if (!valve)
		return (-EINVALID);
	
	if (amount > 100)
		return (-EINVALID);
	
	// create priv element
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return (-EOOM);
	
	priv->set.amount = amount;
	priv->set.sample_intvl = intvl;
	
	// attach created priv to valve
	valve->priv = priv;
	
	// assign function
	valve->valvectrl = valvectrl_sapprox;
	
	return (ALL_OK);
}

/**
 * Constructor for PI valve control
 * @param valve target valve
 * @param intvl sample interval in seconds
 * @param Td deadtime (time elapsed before any change in output is observed after a step change)
 * @param Tu unit step response time
 * @param Ksmax 100% step response output difference
 * @return exec status
 * @note refer to valvectrl_pi() for calculation details
 * @warning tuning factor is hardcoded
 */
int valve_make_pi(struct s_valve * const valve,
		  time_t intvl, time_t Td, time_t Tu, temp_t Ksmax)
{
	struct s_valve_pi_priv * priv = NULL;
	const int tuning_factor = 10;	// XXX aggressive: 1 / moderate: 10 / conservative: 100

	if (!valve)
		return (-EINVALID);

	if ((intvl <= 0) || (Td <= 0) || (Ksmax <= 0))
		return (-EINVALID);

	// ensure sample interval <= (Tu/4) [Nyquist]
	if (intvl > (Tu/4))
		return (-EMISCONFIGURED);

	// create priv element
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return (-EOOM);

	priv->set.sample_intvl = intvl;
	priv->set.Td = Td;
	priv->set.Tu = Tu;
	priv->set.Ksmax = Ksmax;

	priv->run.Tc = (1*Tu > 8*Td) ? 1*Tu : 8*Td;
	priv->run.Tc *= tuning_factor;
	priv->run.Tc /= 10;
	assert(priv->run.Tc);

	priv->run.Kp_u = (float)priv->set.Tu/(priv->set.Td+priv->run.Tc);

	// force reset at first invocation
	priv->run.reset = true;

	// attach created priv to valve
	valve->priv = priv;

	// assign function
	valve->valvectrl = valvectrl_pi;

	return (ALL_OK);
}


/*- SOLAR -*/

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

/*- BOILER -*/

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
 * @todo XXX TODO: implement limit on return temp (p.55/56 / p87-760), (consummer shift / return valve / bypass pump)
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
		heat->run.consumer_shift = 10*(boiler_temp - boiler->set.limit_tmin)/KPRECISIONI;
	}
	else
		heat->run.consumer_shift = 0;
		
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

	// as long as the burner is running we reset the cooldown delay
	if (hardware_relay_get_state(boiler->set.rid_burner_1))
		heat->run.target_consumer_stop_delay = heat->set.consumer_stop_delay;

	return (ALL_OK);
}

/*- HEATSOURCE -*/

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

/*- CIRCUIT -*/

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

	dbgmsg("orig: %.1f, new: %.1f", temp_to_celsius(roundf(source_temp * tld->slope) + tld->offset), temp_to_celsius(t_output));

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
 * @todo review consumer shift formula
 */
static int circuit_run(struct s_heating_circuit * const circuit)
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
			if (circuit->run.target_wtemp && (runtime->consumer_stop_delay > 0)) {
				// disable heat request from this circuit
				circuit->run.heat_request = RWCHCD_TEMP_NOREQUEST;
				water_temp = circuit->run.target_wtemp;
				dbgmsg("in cooldown, remaining: %ld", runtime->consumer_stop_delay);
				goto valve;	// stop processing
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

	// low limit can be overriden by external interferences
	if (water_temp < lwtmin)
		water_temp = lwtmin;

	// save "non-interfered" target water temp
	saved_temp = water_temp;
	
	// interference: handle stop delay requests
	// XXX this will prevent a (valid: small) reduction in output: assumed acceptable
	if (runtime->consumer_stop_delay) {
		dbgmsg("stop delay active, remaining: %ld", runtime->consumer_stop_delay);	// maintain current or higher wtemp during stop delay
		water_temp = (water_temp > circuit->run.target_wtemp) ? water_temp : circuit->run.target_wtemp;
		interference = true;
	}

	// interference: apply global shift - XXX FORMULA
	if (runtime->consumer_shift) {
		water_temp += deltaK_to_temp((0.25F * runtime->consumer_shift));
		interference = true;
	}

	// high limit can never be overriden
	if (water_temp > lwtmax)
		water_temp = lwtmax;
	if (saved_temp > lwtmax)
		saved_temp = lwtmax;

	dbgmsg("request_amb: %.1f, target_amb: %.1f, target_wt: %.1f, curr_wt: %.1f, curr_rwt: %.1f",
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
		ret = valve_tposition(circuit->valve, water_temp);
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

/*- DHWT -*/

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
	dhwt->run.charge_on = false;
	dhwt->run.force_on = false;
	dhwt->run.recycle_on = false;
	dhwt->run.legionella_on = false;
	dhwt->run.charge_overtime = false;
	dhwt->run.mode_since = 0;
	dhwt->run.charge_yday = 0;

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
 * DHWT failsafe routine.
 * By default we stop all pumps and electric self heater.
 * The major inconvenient here is that this failsafe mode COULD provoke a DHWT
 * freeze in the most adverse conditions.
 * @warning DHWT could freeze - TODO: needs review
 * @param dhwt target dhwt
 */
static void dhwt_failsafe(struct s_dhw_tank * restrict const dhwt)
{
	if (dhwt->feedpump)
		pump_set_state(dhwt->feedpump, OFF, FORCE);
	if (dhwt->recyclepump)
		pump_set_state(dhwt->recyclepump, OFF, FORCE);
	if (dhwt->set.rid_selfheater)
		hardware_relay_set_state(dhwt->set.rid_selfheater, OFF, 0);
}

/**
 * DHWT control loop.
 * Controls the dhwt's elements to achieve the desired target temperature.
 * If charge time exceeds the limit, the DHWT will be stopped for the duration
 * of the set limit.
 * @param dhwt target dhwt
 * @return error status
 * @bug pump management for discharge protection needs review
 * @todo XXX TODO: implement dhwprio glissante/absolue for heat request
 * @todo XXX TODO: implement working on electric without sensor
 * @bug discharge protection might fail if the input sensor needs water flow
 * in the feedpump. The solution to this is to implement a fallback to an upstream
 * temperature (e.g. the heatsource's).
 */
static int dhwt_run(struct s_dhw_tank * const dhwt)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t water_temp, top_temp, bottom_temp, curr_temp, wintmax, trip_temp;
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
	if (!valid_tbottom && !valid_ttop) {
		dhwt_failsafe(dhwt);
		return (ret);	// return last error
	}

	// We're good to go
	
	dbgmsg("charge: %d, mode since: %ld, target_temp: %.1f, bottom_temp: %.1f, top_temp: %.1f",
	       dhwt->run.charge_on, dhwt->run.mode_since, temp_to_celsius(dhwt->run.target_temp), temp_to_celsius(bottom_temp), temp_to_celsius(top_temp));
	
	// handle recycle loop
	if (dhwt->recyclepump) {
		if (dhwt->run.recycle_on)
			pump_set_state(dhwt->recyclepump, ON, NOFORCE);
		else
			pump_set_state(dhwt->recyclepump, OFF, NOFORCE);
	}
	
	/* handle heat charge - XXX we enforce sensor position, it SEEMS desirable
	   apply histeresis on logic: trip at target - histeresis (preferably on top sensor),
	   untrip at target (preferably on bottom sensor). */
	if (!dhwt->run.charge_on) {	// no charge in progress
		// check for overtime charge
		limit = SETorDEF(dhwt->set.params.limit_chargetime, runtime->config->def_dhwt.limit_chargetime);
		if (dhwt->run.charge_overtime) {
			if (limit && ((now - dhwt->run.mode_since) <= limit))
				return (ALL_OK); // no further processing, must wait
			else
				dhwt->run.charge_overtime = false;	// reset status
		}
		
		if (valid_ttop)	// prefer top temp if available (trip charge when top is cold)
			curr_temp = top_temp;
		else
			curr_temp = bottom_temp;
		
		// set trip point to (target temp - histeresis)
		if (dhwt->run.force_on)
			trip_temp = dhwt->run.target_temp - deltaK_to_temp(1);	// if forced charge, force histeresis at 1K
		else
			trip_temp = dhwt->run.target_temp - SETorDEF(dhwt->set.params.histeresis, runtime->config->def_dhwt.histeresis);
		
		// trip condition
		if (curr_temp < trip_temp) {
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
			dhwt->run.charge_on = true;
			dhwt->run.mode_since = now;
		}
	}
	else {	// NOTE: untrip should always be last to take precedence, especially because charge can be forced
		if (valid_tbottom)	// prefer bottom temp if available (untrip charge when bottom is hot)
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;
		
		// untrip conditions
		test = false;

		// if heating gone overtime, untrip
		limit = SETorDEF(dhwt->set.params.limit_chargetime, runtime->config->def_dhwt.limit_chargetime);
		if ((limit) && ((now - dhwt->run.mode_since) > limit)) {
			test = true;
			dhwt->run.charge_overtime = true;
		}

		// if heating in progress, untrip at target temp
		if (curr_temp >= dhwt->run.target_temp)
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
			dhwt->run.charge_on = false;
			dhwt->run.mode_since = now;
		}
	}

	return (ALL_OK);
}


/*- PLANT -*/

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
 * Plant summer maintenance operations.
 * When summer conditions are met, the pumps and valves are periodically actuated.
 * The idea of this function is to run as an override filter in the plant_run()
 * loop so that during summer maintenance, the state of these actuators is
 * overriden.
 * @param plant target plant
 * @return exec status
 * @todo sequential run (instead of parallel)
 */
static int plant_summer_maintenance(const struct s_plant * restrict const plant)
{
#define SUMMER_RUN_INTVL	60*60*24*7	///< 1 week
#define SUMMER_RUN_DURATION	60*5		///< 5 minutes
	static time_t timer_start = 0;
	const time_t now = time(NULL);
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	int ret;

	// don't do anything if summer AND plant asleep aren't in effect
	if (!(runtime->summer && runtime->plant_could_sleep))
		timer_start = now;

	// stop running when duration is exceeded (this also prevents running when summer is first triggered)
	if ((now - timer_start) >= (SUMMER_RUN_INTVL + SUMMER_RUN_DURATION)) {
		timer_start = now;
		pr_log("summer maintenance completed");
	}

	// don't run too often
	if ((now - timer_start) < SUMMER_RUN_INTVL)
		return (ALL_OK);

	dbgmsg("summer maintenance active");

	// open all valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_reqopen_full(valvel->valve);

		if (ALL_OK != ret)
			dbgerr("valve_reqopen_full failed on %d (%d)", valvel->id, ret);
	}

	// set all pumps ON
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_set_state(pumpl->pump, ON, NOFORCE);

		if (ALL_OK != ret)
			dbgerr("pump_set_state failed on %d (%d)", pumpl->id, ret);
	}

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
	bool sleeping = true, suberror = false;
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
		
		if (!heatsourcel->heats->run.could_sleep)	// if (a) heatsource isn't sleeping then the plant isn't sleeping
			sleeping = heatsourcel->heats->run.could_sleep;
		
		// max stop delay
		stop_delay = (heatsourcel->heats->run.target_consumer_stop_delay > stop_delay) ? heatsourcel->heats->run.target_consumer_stop_delay : stop_delay;
		runtime->consumer_shift = heatsourcel->heats->run.consumer_shift;	// XXX
	}

	if (runtime->config->summer_maintenance)
		plant_summer_maintenance(plant);

	// run the valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = logic_valve(valvel->valve);
		if (ALL_OK == ret)
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
