//
//  rwchcd_plant.c
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#include <stdio.h>
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"


/**
 * Set pump state.
 * @param pump target pump
 * @param state target pump state
 * @param force_state skips cooldown if true
 * @return error code if any
 */
static int set_pump_state(struct s_pump * const pump, bool state, bool force_state)
{
	time_t cooldown = 0;	// by default, no wait
	
	if (!pump)
		return (-EINVALID);
	
	if (!pump->configured)
		return (-ENOTCONFIGURED);
	
	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!state && !force_state)
		cooldown = pump->actual_cooldown_time ? pump->actual_cooldown_time : pump->set_cooldown_time;
	
	// XXX this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	pump->actual_cooldown_time = set_relay_state(pump->relay, state, cooldown);
}

static int get_pump_state(const struct s_pump * const pump)
{
	if (!pump)
		return (-EINVALID);
	
	if (!pump->configured)
		return (-ENOTCONFIGURED);
	
	// XXX we could return remaining cooldown time if necessary
	return (get_relay_state(pump->relay));
}

/*
 Loi d'eau linaire: pente + offset
 pente calculee negative puisqu'on conserve l'axe des abscisses dans la bonne orientation
 XXX gestion MIN/MAX par caller. TODO implementer courbure
 https://pompe-a-chaleur.ooreka.fr/astuce/voir/111578/le-regulateur-loi-d-eau-pour-pompe-a-chaleur
 http://www.energieplus-lesite.be/index.php?id=10959
 http://herve.silve.pagesperso-orange.fr/regul.htm
 XXX REVISIT FLOATS
 * @param circuit self
 * @param source_temp outdoor temperature to consider
 * @return a target water temperature for this circuit
 */
static temp_t templaw_linear(const struct s_heating_circuit * const circuit, const temp_t source_temp)
{
	//const float out_temp1 = -5.0, water_temp1 = 50.0, out_temp2 = 15.0, water_temp2 = 30.0; // XXX settings
	const temp_t out_temp1 = circuit->tlaw_data.tout1;
	const temp_t water_temp1 = circuit->tlaw_data.twater1;
	const temp_t out_temp2 = circuit->tlaw_data.tout2;
	const temp_t water_temp2 = circuit->tlaw_data.twater2;
	float slope;
	temp_t offset;
	temp_t ambient_measured, ambient_delta, curve_shift;
	temp_t t_output;

	// (Y2 - Y1)/(X2 - X1)
	slope = (water_temp2 - water_temp1) / (out_temp2 - out_temp1);
	// reduction par un point connu
	offset = water_temp2 - (out_temp2 * slope);

	// calculate output at nominal 20C: Y = input*slope + offset
	t_output = source_temp * slope + offset;

	// shift output based on actual target temperature
	curve_shift = (circuit->target_ambient - celsius_to_temp(20)) * (1 - slope);
	t_output += curve_shift;

	// shift based on measured ambient temp (if available) influence p.41
	ambient_measured = get_temp(circuit->id_temp_ambient);
	if (validate_temp(ambient_measured) == ALL_OK) {
		ambient_delta = (circuit->set_ambient_factor/10) * (circuit->target_ambient - ambient_measured);
		curve_shift = ambient_delta * (1 - slope);
		t_output += curve_shift;
	}

	return (t_output);
}

/**
 * implement a linear law for valve position:
 * t_outpout = percent * t_input1 + (1-percent) * t_input2
 * @param valve self
 * @param target_tout target valve output temperature
 * @return valve position in percent or error
 */
static short valvelaw_linear(const struct s_valve * const valve, const temp_t target_tout)
{
	short percent, iterm, iterm_prev;
	temp_t tempin1, tempin2, tempout, error;

	tempin1 = get_temp(valve->id_temp1);
	percent = validate_temp(tempin1);
	if (ALL_OK != percent)
		goto exit;

	// get current outpout
	tempout = get_temp(valve->id_tempout);
	percent = validate_temp(tempout);
	if (ALL_OK != percent)
		goto exit;

	/* if we don't have a sensor for secondary input, guesstimate it
	 treat the provided id as a delta from valve tempout in Celsius XXX REVISIT,
	 tempin2 = tempout - delta */
	if (valve->id_temp2 < 0) {
		tempin2 = tempout - celsius_to_temp(-(valve->id_temp2)); // XXX will need casting
	}
	else {
		tempin2 = get_temp(valve->id_temp2);
		percent = validate_temp(tempin2);
		if (ALL_OK != percent)
			goto exit;
	}

	// XXX IMPLEMENT (P)I to account for actual tempout
	error = target_tout - tempout;
	iterm = Ki * error + iterm_prev;
	iterm_prev = iterm;

	percent = (short)((target_tout - tempin2) / (tempin1 - tempin2) * 100);

	// enforce physical limits
	if (percent > 100)
		percent = 100;
	else if (percent < 0)
		percent = 0;

exit:
	return (percent);
}

/**
 * implement a bang-bang law for valve position:
 * If target_tout > current tempout, open the valve, otherwise close it XXX REVISIT
 * @param valve self
 * @param target_tout target valve output temperature
 * @return valve position in percent or error
 */
static short valvelaw_bangbang(const struct s_valve * const valve, const temp_t target_tout)
{
	short percent;
	temp_t tempout;

	tempout = get_temp(valve->id_tempout);
	percent = validate_temp(tempout);
	if (ALL_OK != percent)
		goto exit;

	if (target_tout > tempout)
		percent = 100;
	else
		percent = 0;

exit:
	return (percent);
}

/**
 * calculate target valve position:
 * @return percent or negative error
 */
static short calc_mixer_pos(const struct s_valve * const mixer, const temp_t target_tout)
{
	short percent;
	temp_t tempout;

	if (!mixer->configured)
		return (-ENOTCONFIGURED);

	if (!mixer->open || !mixer->close)
		return (-EGENERIC);	// XXX REVISIT

	// apply deadzone
	tempout = get_temp(mixer->id_tempout);
	percent = validate_temp(tempout);
	if (percent != ALL_OK)
		return (percent);
	if (((tempout - mixer->deadzone/2) < target_tout) && (target_tout < (tempout + mixer->deadzone/2)))
		return (-EDEADZONE);

	// apply valve law to determine target position
	percent = mixer->valvelaw(mixer, target_tout);

	return (percent);
}

/**
 * Set 3-way mixing valve position
 * @param percent desired position in percent
 */
static inline void set_mixer_pos(struct s_valve * mixer, const short percent)
{
	mixer->target_position = percent;
}

static void valve_offline(struct s_valve * const valve)
{
	set_relay_state(valve->open, OFF, 0);
	set_relay_state(valve->close, OFF, 0);
	valve->action = STOP;
}

/**
 * run valve
 * XXX only handles 3-way valve for now
 */
static int valve_run(struct s_valve * const valve)
{
	const time_t now = time(NULL);
	float time_ratio;
	short percent;

	if (!valve)
		return (-EINVALID);

	if (!valve->configured)
		return (-ENOTCONFIGURED);

	time_ratio = 100.0/valve->ete_time;
	percent = valve->target_position;

	if (valve->action == OPEN)
		valve->position += (now - valve->open->on_since) * time_ratio;
	else if (valve->action == CLOSE)
		valve->position -= (now - valve->close->on_since) * time_ratio;

	// enforce physical limits
	if (valve->position < 0)
		valve->position = 0;
	else if (valve->position > 100)
		valve->position = 100;

	// XXX implement bang-bang valves

	// position is correct
	if (valve->position == percent) {
		// if we're going for full open or full close, make absolutely sure we are
		// XXX REVISIT 2AM CODE
		if (percent == 0) {
			if ((now - valve->close->on_since) < valve->ete_time*4)
				return (ALL_OK);
		}
		else if (percent == 100) {
			if ((now - valve->open->on_since) < valve->ete_time*4)
				return (ALL_OK);
		}

		set_relay_state(valve->open, OFF, 0);
		set_relay_state(valve->close, OFF, 0);
		valve->action = STOP;
	}
	// position is too low
	else if (percent - valve->position > 0) {
		set_relay_state(valve->close, OFF, 0);
		set_relay_state(valve->open, ON, 0);
		valve->action = OPEN;
	}
	// position is too high
	else if (percent - valve->position < 0) {
		set_relay_state(valve->open, OFF, 0);
		set_relay_state(valve->close, ON, 0);
		valve->action = CLOSE;
	}

	return (ALL_OK);
}

/**
 * Put boiler offline.
 * Perform all necessary actions to completely shut down the boiler but
 * DO NOT MARK IT AS OFFLINE.
 */
static int boiler_offline(struct s_boiler * const boiler)
{
	if (!boiler)
		return (-EINVALID);

	if (!boiler->configured)
		return (-ENOTCONFIGURED);

	set_relay_state(boiler->burner_1, OFF, 0);
	set_relay_state(boiler->burner_2, OFF, 0);

	if (boiler->loadpump)
		set_pump_state(boiler->loadpump, OFF, FORCE);

	return (ALL_OK);
}

/**
 * Put boiler online.
 * Perform all necessary actions to prepare the boiler for service but
 * DO NOT MARK IT AS ONLINE.
 * @param boiler target boiler
 * @param return exec status
 */
static int boiler_online(struct s_boiler * const boiler)
{
	temp_t testtemp;
	int ret = -EGENERIC;

	if (!boiler)
		return (-EINVALID);

	if (!boiler->configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(boiler->id_temp);
	ret = validate_temp(testtemp);

	return (ret);
}

static int boiler_antifreeze(struct s_boiler * const boiler)
{
	int ret;
	temp_t boilertemp;

	boilertemp = get_temp(boiler->id_temp);
	ret = validate_temp(boilertemp);

	if (ret)
		return (ret);

	// trip at set_tfreeze point
	if (boilertemp <= boiler->set_tfreeze)
		boiler->antifreeze = true;

	// untrip at limit_tmin + histeresis/2
	if (boiler->antifreeze) {
		if (boilertemp > (boiler->limit_tmin + boiler->histeresis/2))
			boiler->antifreeze = false;
	}

	return (ALL_OK);
}

/**
 * Implement basic single allure boiler
 * @param boiler boiler structure
 * @param target_temp desired boiler temp
 * @return status. If error action must be taken (e.g. offline boiler) XXX REVISIT
 * @todo XXX implmement 2nd allure (p.51)
 * @todo XXX implemment consummer inhibit signal for cool startup
 * @todo XXX implement consummer force signal for overtemp cooldown
 * @todo XXX implement limit on return temp (p.55/56)
 */
static int boiler_run_temp(struct s_boiler * const boiler, temp_t target_temp)
{
	temp_t boiler_temp;
	int ret;

	if (!boiler)
		return (-EINVALID);

	if (!boiler->configured)
		return (-ENOTCONFIGURED);

	// Check if we need antifreeze
	ret = boiler_antifreeze(boiler);
	if (ret)
		return (ret);

	if (!boiler->antifreeze) {	// antifreeze takes over offline mode
		if (!boiler->online)
			return (-EOFFLINE);	// XXX caller must call boiler_offline() otherwise pump will not stop after antifreeze
	}

	// boiler online (or antifreeze)

	if (boiler->loadpump)
		set_pump_state(boiler->loadpump, ON, 0);

	boiler_temp = get_temp(boiler->id_temp);
	ret = validate_temp(boiler_temp);
	if (ret != ALL_OK)
		return (ret);

	// safety checks
	if (boiler_temp > boiler->limit_tmax) {
		set_relay_state(boiler->burner_1, OFF, 0);
		set_relay_state(boiler->burner_2, OFF, 0);
		set_pump_state(boiler->loadpump, ON, FORCE);
		return (-ESAFETY);
	}

	// enforce limits
	if (target_temp < boiler->limit_tmin)
		target_temp = boiler->limit_tmin;
	else if (target_temp > boiler->limit_tmax)
		target_temp = boiler->limit_tmax;

	// save current target
	boiler->target_temp = target_temp;

	// bypass target_temp if antifreeze is active
	if (boiler->antifreeze)
		target_temp = boiler->limit_tmin;

	// temp control
	if (boiler_temp < (target_temp - boiler->histeresis/2))		// trip condition
		set_relay_state(boiler->burner_1, ON, boiler->min_runtime);
	else if (boiler_temp > (target_temp + boiler->histeresis/2))	// untrip condition
		set_relay_state(boiler->burner_1, OFF, boiler->min_runtime);

	return (ALL_OK);
}

static int heatsource_online(const struct s_heat_source * const heat)
{
	if (heat->type == BOILER)
		return (boiler_online(heat->source));
	else
		return (-ENOTIMPLEMENTED);
}

static int heatsource_offline(const struct s_heat_source * const heat)
{
	if (heat->type == BOILER)
		return (boiler_offline(heat->source));
	else
		return (-ENOTIMPLEMENTED);
}

/**
 * XXX currently supports single heat source, all consummers connected to it
 */
static int heatsource_run(struct s_heat_source * const heat)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	temp_t temp, temp_request = 0;

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

	if (heat->type == BOILER)
		return (boiler_run_temp(heat->source, temp_request));
	else
		return (-ENOTIMPLEMENTED);
}

static int circuit_offline(struct s_heating_circuit * const circuit)
{
	if (!circuit)
		return (-EINVALID);

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	circuit->heat_request = 0;
	circuit->target_wtemp = 0;

	if (circuit->pump)
		set_pump_state(circuit->pump, OFF, FORCE);

	set_mixer_pos(circuit->valve, 0);	// XXX REVISIT

	circuit->set_runmode = RM_OFF;

	return (ALL_OK);
}

static int circuit_online(struct s_heating_circuit * const circuit)
{
	temp_t testtemp;
	int ret = -EGENERIC;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	// check that mandatory sensors are working
	testtemp = get_temp(circuit->id_temp_outgoing);
	ret = validate_temp(testtemp);

	return (ret);
}

/**
 * Conditions for running circuit
 * Circuit is off in ANY of the following conditions are met:
 * - t_outdoor > current set_outhoff_MODE
 * - t_outdoor_mixed > current set_outhoff_MODE
 * - t_outdoor_attenuated > current set_outhoff_MODE
 * Circuit is back on if ALL of the following conditions are met:
 * - t_outdoor < current set_outhoff_MODE - set_outhoff_histeresis
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
		default:
			return;	// XXX
	}

	if (!temp_trigger) {	// don't do anything if we have an invalid limit
		circuit->outhoff = false;
		return;	// XXX
	}

	if ((runtime->t_outdoor > temp_trigger) ||
	    (runtime->t_outdoor_mixed > temp_trigger) ||
	    (runtime->t_outdoor_attenuated > temp_trigger)) {
		circuit->outhoff = true;
	}
	else {
		temp_trigger -= circuit->set_outhoff_histeresis;
		if ((runtime->t_outdoor < temp_trigger) &&
		    (runtime->t_outdoor_mixed < temp_trigger) &&
		    (runtime->t_outdoor_attenuated < temp_trigger))
			circuit->outhoff = false;
	}
}


/**
 * XXX ADD optimizations (anticipated turn on/off, boost at turn on, accelerated cool down...)
 * XXX ADD rate of rise cap
 */
static int circuit_run(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t target_temp, water_temp;
	short percent;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	if (!circuit->online)
		return (-EOFFLINE);

	// depending on circuit run mode, assess circuit target temp
	// set valve based on circuit target temp
	if (circuit->set_runmode == RM_AUTO)
		circuit->actual_runmode = runtime->set_runmode;
	else
		circuit->actual_runmode = circuit->set_runmode;

	// Check if the circuit meets outhoff conditions
	circuit_outhoff(circuit);
	if (circuit->outhoff)
		circuit->actual_runmode = RM_OFF;	// if it does, turn it off

	switch (circuit->actual_runmode) {
		case RM_OFF:
			return (circuit_offline(circuit));
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
		case RM_MANUAL:
			set_pump_state(circuit->pump, ON, FORCE);
			return (-1);	//XXX REVISIT
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the circuit is active

	// adjust offset
	target_temp += circuit->set_toffset;

	// save target ambient temp to circuit
	circuit->target_ambient = target_temp;

	// circuit is active, ensure pump is running
	set_pump_state(circuit->pump, ON, 0);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, runtime->t_outdoor_mixed);

	// XXX OPTIM if return temp is known

	// enforce limits
	if (water_temp < circuit->set_limit_wtmin)
		water_temp = circuit->set_limit_wtmin;	// XXX indicator for flooring
	else if (water_temp > circuit->set_limit_wtmax)
		water_temp = circuit->set_limit_wtmax;	// XXX indicator for ceiling

	// XXX cap rate of rise if set

	// save current target water temp
	circuit->target_wtemp = water_temp;

	// apply heat request: water temp + offset
	circuit->heat_request = water_temp + circuit->set_temp_inoffset;

	// adjust valve if necessary
	if (circuit->valve && circuit->valve->configured) {
		percent = calc_mixer_pos(circuit->valve, target_temp);
		if (percent >= 0)
			set_mixer_pos(circuit->valve, percent);
		else {
			valve_offline(circuit->valve);	// XXX REVISIT
			return (percent);
		}
		return (valve_run(circuit->valve));
	}

	return (ALL_OK);
}

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

	return (ret);
}

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
		set_pump_state(dhwt->feedpump, OFF, FORCE);

	if (dhwt->recyclepump)
		set_pump_state(dhwt->recyclepump, OFF, FORCE);

	if (dhwt->selfheater)
		set_relay_state(dhwt->selfheater, OFF, 0);

	dhwt->set_runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * DHW tank control.
 * XXX TODO implement dhwprio glissante/absolue for heat request
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
			set_pump_state(dhwt->feedpump, ON, FORCE);
			set_pump_state(dhwt->recyclepump, ON, FORCE);
			set_relay_state(dhwt->selfheater, ON, 0);
			return (ALL_OK);	//XXX REVISIT
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active

	// handle recycle loop
	if (dhwt->recycle_on)
		set_pump_state(dhwt->recyclepump, ON, NOFORCE);
	else
		set_pump_state(dhwt->recyclepump, OFF, NOFORCE);

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

	// handle heat charge - XXX we enforce sensor position, it SEEMS desirable
	// apply histeresis on logic: trip at target - histeresis, untrip at target
	if (!dhwt->charge_on) {	// heating off
		if (valid_tbottom)	// prefer bottom temp if available
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;

		// if heating not in progress, trip if forced or at (target temp - histeresis)
		if (dhwt->force_on || (curr_temp < (target_temp - dhwt->histeresis))) {
			if (runtime->sleeping && dhwt->selfheater && dhwt->selfheater->configured) {
				// the plant is sleeping and we have a configured self heater: use it
				set_relay_state(dhwt->selfheater, ON, 0);
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
				set_pump_state(dhwt->feedpump, ON, NOFORCE);
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
			// stop self-heater
			set_relay_state(dhwt->selfheater, OFF, 0);

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
			set_pump_state(dhwt->feedpump, OFF, test);


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

int plant_init(const struct s_plant * restrict const plant)
{
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	struct s_heat_source_l * restrict heatsourcel;
	int ret;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);
	
	// online the consummers first
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_online(circuitl->circuit);
		if (ALL_OK != ret) {
			// XXX error handling
			circuit_offline(circuitl->circuit);
			circuitl->circuit->online = false;
		}
		else
			circuitl->circuit->online = true;
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		if (ALL_OK != ret) {
			// XXX error handling
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->online = false;
		}
		else
			dhwtl->dhwt->online = true;
	}

	// finally online the heat source
	heatsourcel = plant->heat_head;	// XXX single heat source
	ret = heatsource_online(heatsourcel->source);
	if (ALL_OK != ret) {
		// XXX error handling
		heatsource_offline(heatsourcel->source);
		heatsourcel->source->online = false;
	}
	else
		heatsourcel->source->online = true;
}

/**
 reduce valve if boiler too low
 use return valve temp to compute output
 degraded mode (when sensors are disconnected)
 keep sensor history
 keep running state across power loss?
 summer run: valve mid position, periodic run of pumps - switchover condition is same as circuit_outhoff with target_temp = preset summer switchover temp
 */
int plant_run(const struct s_plant * restrict const plant)
{
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	struct s_heat_source_l * restrict heatsourcel;
	int ret;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// run the consummers first so they can set their requested heat input
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_run(circuitl->circuit);
		if (ALL_OK != ret) {
			// XXX error handling
			circuit_offline(circuitl->circuit);
			circuitl->circuit->online = false;
		}
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_run(dhwtl->dhwt);
		if (ALL_OK != ret) {
			// XXX error handling
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->online = false;
		}
	}

	// finally run the heat source
	heatsourcel = plant->heat_head;	// XXX single heat source
	ret = heatsource_run(heatsourcel->source);
	if (ALL_OK != ret) {
		// XXX error handling
		heatsource_offline(heatsourcel->source);
		heatsourcel->source->online = false;
	}
}
