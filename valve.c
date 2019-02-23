//
//  valve.c
//  rwchcd
//
//  (C) 2017-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve operation implementation.
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>
#include <math.h>	// truncf
#include <string.h>	// memset

#include "valve.h"
#include "hardware.h"
#include "lib.h"


/**
 * Create a valve
 * @return the newly created valve or NULL
 */
struct s_valve * valve_new(void)
{
	struct s_valve * const valve = calloc(1, sizeof(*valve));
	return (valve);
}

/**
 * Delete a valve.
 * Frees all valve-local resources
 * @param valve the valve to delete
 */
void valve_del(struct s_valve * valve)
{
	if (!valve)
		return;

	free(valve->priv);
	valve->priv = NULL;
	free(valve->name);
	valve->name = NULL;

	free(valve);
}

/**
 * Request valve stop.
 * @param valve target valve
 * @return exec status
 */
int valve_reqstop(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	valve->run.request_action = STOP;
	valve->run.target_course = 0;

	return (ALL_OK);
}

/**
 * Request valve closing/opening amount.
 * @param valve target valve
 * @param perth ‰ amount to open (positive) or close (negative) the valve
 * @return exec status. If requested amount is < valve deadband no action is performed, -EDEADBAND is returned.
 */
int valve_request_pth(struct s_valve * const valve, int_fast16_t perth)
{
	uint_fast16_t tcourse;

	if (!valve)
		return (-EINVALID);

	tcourse = abs(perth);
	// jacket to 100%
	if (tcourse >= 1000)
		tcourse = 1000;

	if (tcourse < valve->set.deadband)
		return (-EDEADBAND);

	valve->run.request_action = (perth < 0) ? CLOSE : OPEN;
	valve->run.target_course = tcourse;

	return (ALL_OK);
}

/**
 * Online callback for PI valve.
 * @param valve self
 * @return exec status
 */
static int v_pi_online(struct s_valve * const valve)
{
	int ret;

	if (!valve)
		return (-EINVALID);

	if (!valve->priv)
		return (-EMISCONFIGURED);

	if (VA_PI != valve->set.algo)
		return (-EMISCONFIGURED);

	// ensure required sensors are configured
	ret = hardware_sensor_clone_time(valve->set.tid_out, NULL);
	if (ALL_OK != ret)
		return (ret);

	return (hardware_sensor_clone_time(valve->set.tid_hot, NULL));
}

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
static int v_pi_control(struct s_valve * const valve, const temp_t target_tout)
{
#define VPI_FPDEC	0x200000	// 10-bit significand, which should never be > 1000pth: good
	struct s_valve_pi_priv * restrict const vpriv = valve->priv;
	const timekeep_t now = timekeep_now();
	int_fast16_t perth;
	temp_t tempin_h, tempin_l, tempout, error;
	int_fast32_t iterm, pterm, output, pthfl, Kp;
	const timekeep_t dt = now - vpriv->run.last_time;
	int ret;
	timekeep_t Ti;

	assert(vpriv);	// checked in online()

	// sample window
	if (dt < vpriv->set.sample_intvl)
		return (ALL_OK);

	vpriv->run.last_time = now;

	// get current outpout
	ret = hardware_sensor_clone_temp(valve->set.tid_out, &tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2))) {
		valve->run.ctrl_ready = false;
		return (-EDEADZONE);
	}

	// get current high input
	ret = hardware_sensor_clone_temp(valve->set.tid_hot, &tempin_h);
	if (ALL_OK != ret)
		return (ret);

	// if we don't have a sensor for low input, guesstimate it
	ret = hardware_sensor_clone_temp(valve->set.tid_cold, &tempin_l);
	if (ALL_OK != ret)
		tempin_l = tempin_h - vpriv->set.Ksmax;

	/*
	 If the current output is out of bound, adjust bounds.
	 This can typically happen if e.g. the valve is open in full,
	 the tid_hot sensor is set as the boiler sensor, and the boiler actual
	 output at the water exhaust is higher than measured by the boiler sensor.
	 Under these circumstances and without this adjustment, if target_tout is
	 higher than tempin_h but lower than tempout, jacketting would still force
	 the valve in full open position.
	 */
	if (tempout > tempin_h)
		tempin_h = tempout;
	else if (tempout < tempin_l)
		tempin_l = tempout;

	// jacketing for saturation
	if (target_tout <= tempin_l) {		// check tempin_l first to prioritize valve closing (useful in case of temporary _h < _l)
		valve_reqclose_full(valve);
		valve->run.ctrl_ready = false;
		return (ALL_OK);
	} else if (target_tout >= tempin_h) {
		valve_reqopen_full(valve);
		valve->run.ctrl_ready = false;
		return (ALL_OK);
	}

	// stop PI operation if inputs are (temporarily) inverted or too close (would make K==0)
	if (tempin_h - tempin_l <= 1000) {
		valve->run.ctrl_ready = false;
		dbgmsg("\"%s\": inputs inverted or input range too narrow", valve->name);
		return (-EDEADZONE);
	}

	// handle algorithm reset
	if (!valve->run.ctrl_ready) {
		vpriv->run.prev_out = tempout;
		vpriv->run.db_acc = 0;
		valve->run.ctrl_ready = true;
		return (ALL_OK);	// skip until next iteration
	}

	/*
	 (tempin_h - tempin_l)/1000 is the process gain K:
	 maximum output delta (Ksmax) / maximum control delta (1000‰).
	 in fact, this could be scaled over a different law to better control
	 non-linear valves, since this computation implicitely assumes the valve
	 is linear.
	 Kp = 1/K * (Tu/(Td+Tc), with Tc is closed-loop time constant: max(A*Tu,B*Td);
	 with [A,B] in [0.1,0.8],[1,8],[10,80] for respectively aggressive, moderate and conservative tunings.
	 Ki = Kp/Ti with Ti integration time. Ti = Tu
	 */
	Kp = vpriv->run.Kp_t * 1000 / (tempin_h - tempin_l);	// Make sure K cannot be 0 here. Kp is already * VPI_FDEC
	Ti = vpriv->set.Tu;

	// calculate error E: (target - actual)
	error = target_tout - tempout;

	// Integral term I: (Ki * error) * sample interval
	iterm = (Kp * error / Ti) * dt;

	// Proportional term P applied to output: Kp * (previous - actual)
	pterm = Kp * (vpriv->run.prev_out - tempout);

	/*
	 Applying the proportional term to the output O avoids kicks when
	 setpoint is changed, however it will also "fight back" against
	 such a change. This negative action will eventually be overcome
	 by the integral term.
	 The benefit of this system is that the algorithm cannot windup
	 and setpoint change does not require specific treatment.
	 */

	output = iterm + pterm;
	pthfl = output + vpriv->run.db_acc;

	/*
	 trunc() so that the algorithm never requests _more_ than what it needs.
	 No need to keep track of the residual since the requested value is
	 an instantaneous calculation at the time of the algorithm run.
	 */
	perth = pthfl / VPI_FPDEC;

	dbgmsg("\"%s\": Kp: %x, E: %x, I: %x, P: %x, O: %x, acc: %x, pthfl: %x, perth: %d",
	       valve->name, Kp, error, iterm, pterm, output, vpriv->run.db_acc, pthfl, perth);

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
	if (valve_request_pth(valve, perth) != ALL_OK)
		vpriv->run.db_acc += iterm;
	else {
		vpriv->run.prev_out = tempout;
		vpriv->run.db_acc = 0;
	}

	return (ALL_OK);
}

/**
 * Online callback for bang-bang valve.
 * @param valve self
 * @return exec status
 */
static int v_bangbang_online(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (VA_BANGBANG != valve->set.algo)
		return (-EMISCONFIGURED);

	// ensure required sensors are configured
	return (hardware_sensor_clone_time(valve->set.tid_out, NULL));
}

/**
 * Implement a bang-bang controller for valve position.
 * If target_tout > current tempout, open the valve, otherwise close it
 * @warning in case of sensor failure, NO ACTION is performed
 * @param valve self
 * @param target_tout target valve output temperature
 * @return exec status
 */
static int v_bangbang_control(struct s_valve * const valve, const temp_t target_tout)
{
	int ret;
	temp_t tempout;

	ret = hardware_sensor_clone_temp(valve->set.tid_out, &tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2)))
		return (-EDEADZONE);	// do nothing

	if (target_tout > tempout)
		valve_reqopen_full(valve);
	else
		valve_reqclose_full(valve);

	return (ALL_OK);
}

/**
 * Online callback for sapprox valve.
 * @param valve self
 * @return exec status
 */
static int v_sapprox_online(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->priv)
		return (-EMISCONFIGURED);

	if (VA_SAPPROX != valve->set.algo)
		return (-EMISCONFIGURED);

	// ensure required sensors are configured
	return (hardware_sensor_clone_time(valve->set.tid_out, NULL));
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
static int v_sapprox_control(struct s_valve * const valve, const temp_t target_tout)
{
	struct s_valve_sapprox_priv * restrict const vpriv = valve->priv;
	const timekeep_t now = timekeep_now();
	temp_t tempout;
	int ret;

	assert(vpriv);	// checked in online()

	// handle reset
	if (!valve->run.ctrl_ready) {
		vpriv->run.last_time = now;
		valve->run.ctrl_ready = true;
	}

	// sample window
	if ((now - vpriv->run.last_time) < vpriv->set.sample_intvl)
		return (ALL_OK);

	vpriv->run.last_time = now;

	ret = hardware_sensor_clone_temp(valve->set.tid_out, &tempout);
	if (ALL_OK != ret)
		return (ret);

	// apply deadzone
	if (((tempout - valve->set.tdeadzone/2) < target_tout) && (target_tout < (tempout + valve->set.tdeadzone/2)))
		return (-EDEADZONE);

	// every sample window time, check if temp is < or > target
	// if temp is < target - deadzone/2, open valve for fixed amount
	if (tempout < target_tout - valve->set.tdeadzone/2) {
		valve_request_pth(valve, vpriv->set.amount);
	}
	// if temp is > target + deadzone/2, close valve for fixed amount
	else if (tempout > target_tout + valve->set.tdeadzone/2) {
		valve_request_pth(valve, -vpriv->set.amount);
	}
	// else stop valve
	else {
		valve_reqstop(valve);
	}

	return (ALL_OK);
}

/**
 * Put valve online.
 * Perform all necessary actions to prepare the valve for service
 * and mark it online.
 * @param valve target valve
 * @return exec status
 */
int valve_online(struct s_valve * const valve)
{
	int ret = ALL_OK;

	if (!valve)
		return (-EINVALID);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	if (VA_NONE == valve->set.algo)
		return (-EMISCONFIGURED);

	if (!valve->set.ete_time)
		return (-EMISCONFIGURED);

	if (!valve->cb.control)
		return (-EMISCONFIGURED);

	if (valve->cb.online)
		ret = valve->cb.online(valve);

	// return to idle
	valve_reqstop(valve);

	// reset the control algorithm
	valve->run.ctrl_ready = false;

	if (ALL_OK == ret)
		valve->run.online = true;

	return (ret);
}

/**
 * Shutdown valve.
 * Perform all necessary actions to completely shut down the valve.
 * @param valve target valve
 * @return exec status
 */
int valve_shutdown(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->run.active)
		return (ALL_OK);

	// close valve
	valve_reqclose_full(valve);

	// reset the control algorithm
	valve->run.ctrl_ready = false;

	valve->run.active = false;

	return (ALL_OK);
}

/**
 * Put valve offline.
 * Perform all necessary actions to completely shut down the valve
 * and mark it offline.
 * @param valve target valve
 * @return exec status
 */
int valve_offline(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	// stop the valve uncondiditonally
	hardware_relay_set_state(valve->set.rid_hot, OFF, 0);
	hardware_relay_set_state(valve->set.rid_cold, OFF, 0);

	memset(&valve->run, 0x00, sizeof(valve->run));
	//valve->run.ctrl_ready = false;	// handled by memset
	//valve->run.online = false;		// handled by memset

	return (ALL_OK);
}

#define VALVE_MAX_RUNX	3	///< sets maximum continuous actuation request in one direction as ete_time * VALVE_MAX_RUNX
/**
 * Valve logic.
 * Ensures the valve cannot run forever in one direction.
 * Flags when the valve has reached either end at least once.
 * @param valve the target valve
 * @return exec status
 */
int valve_logic(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->run.online)
		return (-EOFFLINE);

	if (OPEN == valve->run.request_action) {
		if (valve->run.acc_open_time >= valve->set.ete_time * VALVE_MAX_RUNX) {
			valve->run.true_pos = true;
			valve_reqstop(valve);	// don't run if we're already maxed out
		}
	}
	else if (CLOSE == valve->run.request_action) {
		if (valve->run.acc_close_time >= valve->set.ete_time * VALVE_MAX_RUNX) {
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
 * otherwise dt will be out of whack (this is normally ensured by valve_online()).
 * @note the function assumes that the sanity of the valve argument will be checked before invocation.
 * @warning beware of the resolution limit on valve end-to-end time
 * @warning REVIEW: overshoots
 */
int valve_run(struct s_valve * const valve)
{
	const timekeep_t now = timekeep_now();
	timekeep_t dt;
	float perth_ptk;	// ‰ position change per tick
	int_fast16_t course;
	int ret = ALL_OK;

	if (!valve)
		return (-EINVALID);

	if (!valve->run.online)
		return (-EOFFLINE);

	dt = now - valve->run.last_run_time;
	perth_ptk = 1000.0F/valve->set.ete_time;

	valve->run.last_run_time = now;
	valve->run.active = true;		// XXX never set false because we don't really need to for now

	course = roundf(dt * perth_ptk);		// we don't keep track of residual because we're already in ‰.

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
				ret = hardware_relay_set_state(valve->set.rid_cold, OFF, 0);	// break before make
				if (ALL_OK != ret)
					goto fail;
				ret = hardware_relay_set_state(valve->set.rid_hot, ON, 0);
				if (ALL_OK != ret)
					goto fail;
				valve->run.actual_action = OPEN;
				break;
			case CLOSE:
				ret = hardware_relay_set_state(valve->set.rid_hot, OFF, 0);	// break before make
				if (ALL_OK != ret)
					goto fail;
				ret = hardware_relay_set_state(valve->set.rid_cold, ON, 0);
				if (ALL_OK != ret)
					goto fail;
				valve->run.actual_action = CLOSE;
				break;
			default:
				ret = -EINVALID;
			case STOP:
				ret = hardware_relay_set_state(valve->set.rid_hot, OFF, 0);
				if (ALL_OK != ret)
					goto fail;
				ret = hardware_relay_set_state(valve->set.rid_cold, OFF, 0);
				if (ALL_OK != ret)
					goto fail;
				valve->run.actual_action = STOP;
				break;
		}
	}

	dbgmsg("\"%s\": rq_act: %d, act: %d, pos: %.1f%%, rq_crs: %.1f%%",
	       valve->name, valve->run.request_action, valve->run.actual_action, (float)valve->run.actual_position/10.0F,
	       (float)valve->run.target_course/10.0F);

fail:
	return (ret);
}

/**
 * Constructor for bangbang valve control.
 * This controller requires @b tid_out to be set.
 * This controller ignores @b tid_hot and @b tid_cold
 * @param valve target valve
 * @return exec status
 */
int valve_make_bangbang(struct s_valve * const valve)
{
	if (!valve)
		return (-EINVALID);

	if (VA_NONE != valve->set.algo)
		return (-EEXISTS);

	valve->cb.online = v_bangbang_online;
	valve->cb.control = v_bangbang_control;
	valve->set.algo = VA_BANGBANG;

	return (ALL_OK);
}

/**
 * Constructor for sapprox valve control.
 * This controller requires @b tid_out to be set.
 * This controller ignores @b tid_hot and @b tid_cold
 * @param valve target valve
 * @param amount movement amount in %
 * @param intvl sample interval
 * @return exec status
 * @warning should ensure that the sample interval allows full amount movement
 */
int valve_make_sapprox(struct s_valve * const valve, uint_fast8_t amount, timekeep_t intvl)
{
	struct s_valve_sapprox_priv * priv = NULL;

	if (!valve)
		return (-EINVALID);

	if ((VA_NONE != valve->set.algo) || (valve->priv))
		return (-EEXISTS);

	if ((amount > 100) || (intvl < 1))
		return (-EINVALID);

	// create priv element
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return (-EOOM);

	priv->set.amount = amount;
	priv->set.sample_intvl = intvl;

	// attach created priv to valve
	valve->priv = priv;

	// assign callbacks
	valve->cb.online = v_sapprox_online;
	valve->cb.control = v_sapprox_control;

	valve->set.algo = VA_SAPPROX;

	return (ALL_OK);
}

/**
 * Constructor for PI valve control.
 * This controller requires @b tid_hot and @b tid_out to be set.
 * This controller recommends @b tid_cold to be set.
 * @param valve target valve
 * @param intvl sample interval
 * @param Td deadtime (time elapsed before any change in output is observed after a step change)
 * @param Tu unit step response time
 * @param Ksmax 100% step response output difference. Used if it cannot be measured.
 * @param t_factor tuning factor: aggressive: 1 / moderate: 10 / conservative: 100
 * @return exec status
 * @note refer to valvectrl_pi() for calculation details
 */
int valve_make_pi(struct s_valve * const valve,
		  timekeep_t intvl, timekeep_t Td, timekeep_t Tu, temp_t Ksmax, uint_fast8_t t_factor)
{
	struct s_valve_pi_priv * priv = NULL;

	if (!valve)
		return (-EINVALID);

	if ((VA_NONE != valve->set.algo) || (valve->priv))
		return (-EEXISTS);

	if ((intvl <= 0) || (Td <= 0) || (Ksmax <= 0) || (t_factor <= 0))
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
	priv->set.tune_f = t_factor;

	priv->run.Tc = (1*Tu > 8*Td) ? 1*Tu : 8*Td;	// see valvectrl_pi()
	priv->run.Tc *= t_factor;
	priv->run.Tc /= 10;
	assert(priv->run.Tc);

	priv->run.Kp_t = ((priv->set.Tu * VPI_FPDEC) + ((priv->set.Td + priv->run.Tc)/2)) / (priv->set.Td + priv->run.Tc);
							// ^--- manual rounding, Td/Tc always >=0
	// attach created priv to valve
	valve->priv = priv;

	// assign callbacks
	valve->cb.online = v_pi_online;
	valve->cb.control = v_pi_control;

	valve->set.algo = VA_PI;

	return (ALL_OK);
}
