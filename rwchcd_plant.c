//
//  rwchcd_plant.c
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#include <stdio.h>
#include "rwchcd_plant.h"


/*
 Loi d'eau linaire: pente + offset
 pente calculee negative puisqu'on conserve l'axe des abscisses dans la bonne orientation
 XXX gestion MIN/MAX par caller. TODO implementer courbure
 https://pompe-a-chaleur.ooreka.fr/astuce/voir/111578/le-regulateur-loi-d-eau-pour-pompe-a-chaleur
 http://www.energieplus-lesite.be/index.php?id=10959
 */
static float templaw_linear(const struct * const s_heating_circuit circuit, const float source_temp)
{
	//const float out_temp1 = -5.0, water_temp1 = 50.0, out_temp2 = 15.0, water_temp2 = 30.0; // XXX settings
	const float out_temp1 = circuit->tlaw_data->tout1;
	const float water_temp1 = circuit->tlaw_data->twater1;
	const float out_temp2 = circuit->tlaw_data->tout2;
	const float water_temp2 = circuit->tlaw_data->twater2;
	float slope, offset;

	// (Y2 - Y1)/(X2 - X1)
	slope = (water_temp2 - water_temp1) / (out_temp2 - out_temp1);
	// reduction par un point connu
	offset = water_temp2 - (out_temp2 * slope);

	// Y = input*slope + offset
	return (source_temp * slope + offset);
}

/**
 * implement a linear law for valve position:
 * t_outpout = percent * t_input1 + (1-percent) * t_input2
 * @param valve self
 * @param target_tout target valve output temperature
 * @return valve position in percent or error
 */
static short valvelaw_linear(const struct * const s_valve valve, const temp_t target_tout)
{
	short percent;
	temp_t tempin1, tempin2, tempout;

	// if we don't have a sensor for secondary input, guesstimate it
	tempin1 = get_temp(mixer->id_temp1);
	percent = validate_temp(tempin1);
	if (percent != ALL_OK)
		goto exit;

	if (mixer->id_temp2 < 0) {
		tempout = get_temp(mixer->id_tempout);
		percent = validate_temp(tempout);
		if (percent != ALL_OK)
			goto exit;
		tempin2 = tempout - celsius_to_temp(mixer->id_temp2);
	}
	else {
		tempin2 = get_temp(mixer->id_temp2);
		percent = validate_temp(tempin2);
		if (percent != ALL_OK)
			goto exit;
	}

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
 * calculate target valve position:
 * @return percent or negative error
 */
static short calc_mixer_pos(const struct s_valve * const mixer, const temp_t target_tout)
{
	short percent;
	temp_t tempin1, tempin2, tempout;

	if (!mixer->configured)
		return (-ENOTCONFIGURED);

	if (!mixer->open || !mixer->close)
		return (-EGENERIC);	// XXX REVISIT

	// apply deadzone
	tempout = get_temp(mixer->id_tempout);
	percent = validate_temp(tempout);
	if (percent != ALL_OK)
		return (percent);
	if ((tempout - mixer->deadzone/2) < target_tout) && (target_tout < (tempout + mixer->deadzone/2)))
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

void valve_offline(struct s_valve * const valve)
{
	set_relay_state(valve->open, OFF, 0);
	set_relay_state(valve->close, OFF, 0);
	valve->action = STOP;
}

/**
 * run valve
 * XXX only handles 3-way valve for now
 */
static int run_valve(const struct s_valve * const valve)
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
		valve->position += (now - valve->open.on_since) * time_ratio;
	else if (valve->action == CLOSE)
		valve->position -= (now - valve->close.on_since) * time_ratio;

	// enforce physical limits
	if (valve->position < 0)
		valve->position = 0;
	else if (valve->position > 100)
		valve->position = 100;

	// position is correct
	if (valve->position == percent) {
		// if we're going for full open or full close, make absolutely sure we are
		// XXX REVISIT 2AM CODE
		if (percent == 0) {
			if ((now - valve->close.on_since) < valve->ete_time*4)
				return (ALL_OK);
		}
		else if (percent == 100) {
			if ((now - valve->open.on_since) < valve->ete_time*4)
				return (ALL_OK);
		}

		set_relay_state(valve->open, OFF, 0);
		set_relay_state(valve->close, OFF, 0);
		valve->action = STOP;
	}

	// position is too low
	if (percent - valve->position > 0) {
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
		set_pump_state(boiler->loadpump, OFF);

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

/**
 * Implement basic single allure boiler
 * @param boiler boiler structure
 * @param target_temp desired boiler temp
 * @return status. If error action must be taken (e.g. offline boiler) XXX REVISIT
 */
static int boiler_run_temp(struct s_boiler * const boiler, const temp_t target_temp)
{
	temp_t boiler_temp;
	int ret;

	if (!boiler)
		return (-EINVALID);

	if (!boiler->configured)
		return (-ENOTCONFIGURED);

	if (!boiler->online)
		return (-EOFFLINE);

	// boiler online

	if (boiler->loadpump)
		set_pump_state(boiler->loadpump, ON)

	boiler_temp = get_temp(boiler->id_temp);
	ret = validate_temp(boiler_temp);
	if (ret != ALL_OK)
		return (ret):

	// safety checks
	if (boiler_temp > boiler->limit_tmax) {
		set_relay_state(boiler->burner_1, OFF, 0);
		set_relay_state(boiler->burner_2, OFF, 0);
		set_pump_state(boiler->loadpump, ON);
		return (-ESAFETY);
	}

	// enforce limits
	if (target_temp < boiler->limit_tmin)
		target_temp = boiler->limit_tmin;
	else if (target_temp > boiler->limit_tmax)
		target_temp = boiler->limit_tmax;

	// save current target
	boiler->target_temp = target_temp;

	// temp control
	if (boiler_temp < (target_temp - boiler->histeresis/2))
		set_relay_state(boiler->burner_1, ON, boiler->min_runtime);
	else if (boiler_temp > (target_temp + boiler->histeresis/2))
		set_relay_state(boiler->burner_1, OFF, boiler->min_runtime);

	return (ALL_OK);

}

static int run_heat_source(const struct s_heat_source * const heat)
{
	temp_t temp_request;

	if (!heat->configured)
		return (-ENOTCONFIGURED);

	// for consummers in runtime scheme, collect heat requests and max them

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
		set_pump_state(dhwt->feedpump, OFF);

	set_mixer_pos(circuit->valve, 0);	// XXX REVISIT

	circuit->runmode = RM_OFF;

	return (ALL_OK);
}

int circuit_online(struct s_heating_circuit * const circuit)
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


static int run_circuit(struct s_heating_circuit * const circuit)
{
	temp_t target_temp, water_temp;
	enum e_runmode mode;
	short percent;

	if (!circuit)
		return (-EINVALID);

	if (!circuit->configured)
		return (-ENOTCONFIGURED);

	if (!circuit->online)
		return (-EOFFLINE);

	// depending on circuit run mode, assess circuit target temp
	// set valve based on circuit target temp
	if (circuit->runmode == RM_AUTO)
		mode = runtime->runmode;
	else
		mode = circuit->runmode;

	switch (mode) {
		case RM_OFF:
			return (circuit_offline(circuit));
		case RM_COMFORT:
			target_temp = circuit->target_tcomfort;
			break;
		case RM_ECO:
			target_temp = circuit->target_teco;
			break;
		case RM_DHWONLY:
		case RM_FROSTFREE:
			target_temp = circuit->target_tfrostfree;
			break;
		case RM_MANUAL:
			set_pump_state(circuit->pump->relay, ON);
			return (-1);	//XXX REVISIT
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the circuit is active

	target_temp += circuit->target_toffset;

	// circuit is active, ensure pump is running
	set_pump_state(circuit->pump, ON);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, target_temp);

	// enforce limits
	if (water_temp < circuit->limit_wtmin)
		water_temp = circuit->limit_wtmin;
	else if (water_temp > circuit->limit_wtmax)
		water_temp = circuit->limit_wtmax;

	// save current target water temp
	circuit->target_wtemp = water_temp;

	// apply heat request
	circuit->heat_request = water_temp + circuit->temp_inoffset;

	// adjust valve if necessary
	if (circuit->valve) {
		percent = calc_mixer_pos(circuit->valve, target_temp);
		if (percent >= 0)
			set_mixer_pos(circuit->valve, percent);
		else {
			valve_offline(circuit->valve);	// XXX REVISIT
			return (percent);
		}
		return (run_valve(circuit->valve));
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
	if (ret != ALL_OK) {
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
	dhwt->heating_on = false;
	dhwt->recycle_on = false;

	if (dhwt->feedpump)
		set_pump_state(dhwt->feedpump, OFF);

	if (dhwt->recyclepump)
		set_pump_state(dhwt->recyclepump, OFF);

	if (dhwt->selfheater)
		set_relay_state(dhwt->selfheater, OFF, 0);

	dhwt->runmode = RM_OFF;

	return (ALL_OK);
}

static int run_dhwt(struct s_dhw_tank * const dhwt)
{
	temp_t target_temp, water_temp, curr_temp;
	enum e_runmode mode;
	int ret = -EGENERIC;

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->configured)
		return (-ENOTCONFIGURED);

	if (!dhwt->online)
		return (-EOFFLINE);

	// depending on dhwt run mode, assess dhwt target temp
	if (dhwt->runmode == RM_AUTO)
		mode = runtime->dhwmode;
	else
		mode = dhwt->runmode;

	switch (mode) {
		case RM_OFF:
			return (dhwt_offline(dhwt));
		case RM_COMFORT:
			target_temp = circuit->target_tcomfort;
			break;
		case RM_ECO:
			target_temp = circuit->target_teco;
			break;
		case RM_FROSTFREE:
			target_temp = circuit->target_tfrostfree;
			break;
		case RM_MANUAL:
			set_pump_state(dhwt->feedpump, ON);
			set_pump_state(dhwt->recyclepump, ON);
			set_relay_state(dhwt->selfheater, ON, 0);
			return (ALL_OK);	//XXX REVISIT
		default:
			return (-EINVALIDMODE);
	}

	// if we reached this point then the dhwt is active

	// handle recycle loop
	if (dhwt->recycle_on)
		set_pump_state(dhwt->recyclepump, ON);
	else
		set_pump_state(dhwt->recyclepump, OFF);

	// enforce limits on dhw temp
	if (target_temp < dhwt->limit_tmin)
		target_temp = dhwt->limit_tmin;
	else if (target_temp > dhwt->limit_tmax)
		target_temp = dhwt->limit_tmax;

	// save current target dhw temp
	dhwt->target_temp = target_temp;

	// apply histeresis - XXX REVISIT CURRENTLY ONLY USING A SINGLE SENSOR
	curr_temp = get_temp(dhwt->id_temp_bottom);
	ret = validate_temp(boiler_temp);
	if (ret != ALL_OK) {
		curr_temp = get_temp(dhwt->id_temp_top);
		ret = validate_temp(boiler_temp);
	}
	if (ret != ALL_OK)
		return (ret):

	if (dhwt->heating_on) {
		// if heating in progress, untrip at target temp
		if (curr_temp > target_temp) {
			// stop feedpump
			set_pump_state(dhwt->feedpump, OFF);

			// set heat request to minimum
			dhwt->heat_request = dhwt->limit_wintmin;

			// mark heating as done
			dhwt->heating_on = false;
		}
	}
	else {	// heating off
		// if heating not in progress, trip at target temp - histeresis
		if (curr_temp > target_temp - dhwt->histeresis) {
			// calculate necessary water feed temp
			water_temp = target_temp + dhwt->temp_inoffset;

			// enforce limits
			if (water_temp < dhwt->limit_wintmin)
				water_temp = dhwt->limit_wintmin;
			else if (water_temp > dhwt->limit_wintmax)
				water_temp = dhwt->limit_wintmax;

			// apply heat request
			dhwt->heat_request = water_temp;

			// start feedpump
			set_pump_state(dhwt->feedpump, ON);

			// mark heating in progress
			dhwt->heating_on = true;
		}
	}

	return (ALL_OK);
}

/**
 reduce valve if boiler too low
 use return valve temp to compute output
 degraded mode (when sensors are disconnected)
 keep sensor history
 summer run: valve mid position, periodic run of pumps
 */
static int run_plant()
{
	run_circuit();
	run_valve();

	if (run_dhw(dhwt) != ALL_OK) {
		dhwt_offline(dhwt);
		dhwt->online = false;	// XXX add a last_error element to structs?
	}

	run_heat_source();
}
