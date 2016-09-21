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
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_logic.h"
#include "rwchcd_plant.h"

/** PUMP **/

/**
 * Create a new pump
 * @return pointer to the created pump
 */
struct s_pump * pump_new(void)
{
	struct s_pump * const pump = calloc(1, sizeof(struct s_pump));

	return (pump);
}

/**
 * Delete a pump
 * @param pump the pump to delete
 */
static void pump_del(struct s_pump * pump)
{
	if (!pump)
		return;

	hardware_relay_del(pump->relay);
	free(pump->name);
	free(pump);
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
	
	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!state && !force_state)
		cooldown = pump->actual_cooldown_time ? pump->actual_cooldown_time : pump->set_cooldown_time;
	
	// XXX this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	pump->actual_cooldown_time = hardware_relay_set_state(pump->relay, state, cooldown);

	return (ALL_OK);
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
	float Ki;	// XXX REVISIT

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

	// XXX IMPLEMENT (P)I to account for actual tempout - XXX not used yet
	error = target_tout - tempout;
	iterm = Ki * error + iterm_prev;
	iterm_prev = iterm;

	percent = ((target_tout - tempin2) / (tempin1 - tempin2) * 100);

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
 * If target_tout > current tempout, open the valve, otherwise close it
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
 * calculate mixer valve target position:
 * @param mixer target valve
 * @param target_tout target temperature at output of mixer
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
 * Create a new valve
 * @return pointer to the created valve
 */
struct s_valve * valve_new(void)
{
	struct s_valve * const valve = calloc(1, sizeof(struct s_valve));

	return (valve);
}

/**
 * Delete a valve
 * @param valve the valve to delete
 */
static void valve_del(struct s_valve * valve)
{
	if (!valve)
		return;

	hardware_relay_del(valve->open);
	hardware_relay_del(valve->close);
	free(valve->name);
	free(valve);
}

/**
 * Offline a valve - XXX REVISIT: non permanent, API non consistent with others
 * @param valve target valve
 */
static void valve_offline(struct s_valve * const valve)
{
	hardware_relay_set_state(valve->open, OFF, 0);
	hardware_relay_set_state(valve->close, OFF, 0);
	valve->action = STOP;
}

/**
 * run valve
 * @param valve target valve
 * @return error status
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

	time_ratio = 100.0F/valve->ete_time;
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

		hardware_relay_set_state(valve->open, OFF, 0);
		hardware_relay_set_state(valve->close, OFF, 0);
		valve->action = STOP;
	}
	// position is too low
	else if (percent - valve->position > 0) {
		hardware_relay_set_state(valve->close, OFF, 0);
		hardware_relay_set_state(valve->open, ON, 0);
		valve->action = OPEN;
	}
	// position is too high
	else if (percent - valve->position < 0) {
		hardware_relay_set_state(valve->open, OFF, 0);
		hardware_relay_set_state(valve->close, ON, 0);
		valve->action = CLOSE;
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

	pump_del(solar->pump);
	free(solar->name);
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
		boiler->histeresis = delta_to_temp(6);
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

	pump_del(boiler->loadpump);
	hardware_relay_del(boiler->burner_1);
	hardware_relay_del(boiler->burner_2);

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
	int ret = -EGENERIC;

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
		pump_set_state(boiler->loadpump, OFF, FORCE);

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
		if (boilertemp > (boiler->limit_tmin + boiler->histeresis/2))
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
	temp_t boiler_temp, target_temp;
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

	// temp control
	if (boiler_temp < (target_temp - boiler->histeresis/2))		// trip condition
		hardware_relay_set_state(boiler->burner_1, ON, 0);	// immediate start
	else if (boiler_temp > (target_temp + boiler->histeresis/2))	// untrip condition
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
 XXX REVISIT FLOATS
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
 * Put circuit online.
 * Perform all necessary actions to prepare the circuit for service but
 * DO NOT MARK IT AS ONLINE.
 * @param circuit target circuit
 * @param return exec status
 */
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
		pump_set_state(circuit->pump, OFF, FORCE);

	circuit->valve->target_position = 0;	// XXX REVISIT

	circuit->set_runmode = RM_OFF;

	return (ALL_OK);
}

/**
 * Circuit control loop.
 * Controls the circuits elements to achieve the desired target temperature.
 * @param circuit target circuit
 * @return exec status
 * XXX safety for heating floor if implementing positive consummer_shift()
 */
static int circuit_run(struct s_heating_circuit * const circuit)
{
	const struct s_runtime * restrict const runtime = get_runtime();
	temp_t water_temp;
	short percent;

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
			pump_set_state(circuit->pump, ON, FORCE);
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

	// save calculated target ambient temp to circuit
	circuit->target_ambient = circuit->request_ambient + circuit->set_toffset;

	// circuit is active, ensure pump is running
	pump_set_state(circuit->pump, ON, 0);

	// calculate water pipe temp
	water_temp = circuit->templaw(circuit, runtime->t_outdoor_mixed);

	// enforce limits
	if (water_temp < circuit->limit_wtmin)
		water_temp = circuit->limit_wtmin;	// XXX indicator for flooring
	else if (water_temp > circuit->limit_wtmax)
		water_temp = circuit->limit_wtmax;	// XXX indicator for ceiling

	// save current target water temp
	circuit->target_wtemp = water_temp;

	// apply heat request: water temp + offset
	circuit->heat_request = water_temp + circuit->set_temp_inoffset;

	// adjust valve if necessary
	if (circuit->valve && circuit->valve->configured) {
		percent = calc_mixer_pos(circuit->valve, circuit->target_ambient);
		if (percent >= 0)
			circuit->valve->target_position = percent;
		else {
			valve_offline(circuit->valve);	// XXX REVISIT
			return (percent);
		}
		return (valve_run(circuit->valve));
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
		pump_set_state(dhwt->feedpump, OFF, FORCE);

	if (dhwt->recyclepump)
		pump_set_state(dhwt->recyclepump, OFF, FORCE);

	if (dhwt->selfheater)
		hardware_relay_set_state(dhwt->selfheater, OFF, 0);

	dhwt->set_runmode = RM_OFF;

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

	/* handle heat charge - XXX we enforce sensor position, it SEEMS desirable
	   apply histeresis on logic: trip at target - histeresis (preferably on low sensor),
	   untrip at target (preferably on high sensor). */
	if (!dhwt->charge_on) {	// heating off
		if (valid_tbottom)	// prefer bottom temp if available
			curr_temp = bottom_temp;
		else
			curr_temp = top_temp;

		// if heating not in progress, trip if forced or at (target temp - histeresis)
		if (dhwt->force_on || (curr_temp < (target_temp - dhwt->histeresis))) {
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

	valve_del(circuit->valve);
	pump_del(circuit->pump);
	free(circuit->name);

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
	pump_del(dhwt->feedpump);
	pump_del(dhwt->recyclepump);
	hardware_relay_del(dhwt->selfheater);
	free(dhwt->name);

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

	free(source->name);
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
	struct s_heating_circuit_l * circuitelement, * circuitlnext;
	struct s_dhw_tank_l * dhwtelement, * dhwtlnext;
	struct s_heatsource_l * sourceelement, * sourcenext;

	// clear all registered circuits
	circuitelement = plant->circuit_head;
	while (circuitelement) {
		circuitlnext = circuitelement->next;
		del_circuit(circuitelement->circuit);
		free(circuitelement);
		plant->circuit_n--;
		circuitelement = circuitlnext;
	}

	// clear all registered dhwt
	dhwtelement = plant->dhwt_head;
	while (dhwtelement) {
		dhwtlnext = dhwtelement->next;
		del_dhwt(dhwtelement->dhwt);
		free(dhwtelement);
		plant->dhwt_n--;
		dhwtelement = dhwtlnext;
	}

	// clear all registered heatsources
	sourceelement = plant->heats_head;
	while (sourceelement) {
		sourcenext = sourceelement->next;
		del_heatsource(sourceelement->heats);
		free(sourceelement);
		plant->heats_n--;
		sourceelement = sourcenext;
	}

	free(plant);
}

/**
 * Bring plant online.
 * @param plant target plant
 * @return error status
 * @note REQUIRES valid sensor values before being called
 */
int plant_online(struct s_plant * restrict const plant)
{
	struct s_heating_circuit_l * restrict circuitl;
	struct s_dhw_tank_l * restrict dhwtl;
	struct s_heatsource_l * restrict heatsourcel;
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
			dbgerr("circuit_online failed: %d", ret);
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
			dbgerr("dhwt_online failed: %d", ret);
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->online = false;
		}
		else
			dhwtl->dhwt->online = true;
	}

	// finally online the heat source
	heatsourcel = plant->heats_head;	// XXX single heat source
	ret = heatsource_online(heatsourcel->heats);
	if (ALL_OK != ret) {
		// XXX error handling
		dbgerr("heatsource_online failed: %d", ret);
		heatsource_offline(heatsourcel->heats);
		heatsourcel->heats->online = false;
	}
	else
		heatsourcel->heats->online = true;
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

	// reflect global sleeping state
	runtime->sleeping = sleeping;

	return (ALL_OK);	// XXX
}
