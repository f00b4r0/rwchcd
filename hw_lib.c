//
//  hw_lib.c
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware-related functions library.
 * @todo convert to fixed-point arithmetic.
 */

#include <stdint.h>
#include <math.h>	// sqrtf
#include <assert.h>

#include "filecfg.h"
#include "filecfg_parser.h"
#include "lib.h"
#include "timekeep.h"
#include "hw_lib.h"


// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

/**
 * Convert resistance value to actual temperature based on Callendar - Van Dusen.
 * Use a quadratic fit for simplicity.
 * - http://aviatechno.net/thermo/rtd03.php
 * - https://www.newport.com/medias/sys_master/images/images/h4b/h16/8797291446302/TN-RTD-1-Callendar-Van-Dusen-Equation-and-RTD-Temperature-Sensors.pdf
 * - Rt = R0 + R0*alpha*[t - delta*(t/100 - 1)*(t/100) - beta*(t/100 - 1)*(t/100)^3]
 * - alpha is the mean R change referred to 0C
 * - Rt = R0 * [1 + A*t + B*t^2 - C*(t-100)*t^3]
 * - A = alpha + (alpha*delta)/100
 * - B = - (alpha * delta)/(100^2)
 * - C = - (alpha * beta)/(100^4)
 * @param R0 nominal resistance at 0C
 * @param A precomputed A parameter
 * @param B precomputed B parameter
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
__attribute__((const)) static float quadratic_cvd(const float R0, const float A, const float B, const uint_fast16_t ohm)
{
	// quadratic fit: we're going to ignore the cubic term given the temperature range we're looking at
	return ((-R0*A + sqrtf(R0*R0*A*A - 4.0F*R0*B*(R0 - ohm))) / (2.0F*R0*B));
}

/**
 * Convert Pt1000 resistance value to actual temperature.
 * Use European Standard values.
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
__attribute__((const)) static float pt1000_ohm_to_celsius(const uint_fast16_t ohm)
{
	const float R0 = 1000.0F;
	const float alpha = 0.003850F;
	const float delta = 1.4999F;

	// Callendar - Van Dusen parameters
	const float A = alpha + (alpha * delta) / 100;
	const float B = (-alpha * delta) / (100 * 100);
	//C = (-alpha * beta) / (100 * 100 * 100 * 100);	// only for t < 0

	return (quadratic_cvd(R0, A, B, ohm));
}

/**
 * Convert Ni1000 resistance value to actual temperature.
 * Use DIN 43760 with temp coef of 6178ppm/K.
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
__attribute__((const)) static float ni1000_ohm_to_celsius(const uint_fast16_t ohm)
{
	const float R0 = 1000.0F;
	const float A = 5.485e-3;
	const float B = 6.650e-6;

	return (quadratic_cvd(R0, A, B, ohm));
}

/**
 * Return a sensor ohm to celsius converter callback based on sensor type.
 * @param stype the sensor type identifier
 * @return correct function pointer for sensor type or NULL if invalid type
 */
__attribute__ ((pure)) ohm_to_celsius_ft * hw_lib_sensor_o_to_c(const enum e_hw_stype stype)
{
	switch (stype) {
		case HW_ST_PT1000:
			return (pt1000_ohm_to_celsius);
		case HW_ST_NI1000:
			return (ni1000_ohm_to_celsius);
		case HW_ST_NONE:
		default:
			return (NULL);
	}
}

/**
 * Dump a hardware sensor to config.
 * @param sensor the sensor to dump
 */
void hw_lib_filecfg_sensor_dump(const struct s_hw_sensor * const sensor)
{
	const char * type;

	if (!sensor->set.configured)
		return;

	switch (sensor->set.type) {
		case HW_ST_PT1000:
			type = "PT1000";
			break;
		case HW_ST_NI1000:
			type = "NI1000";
			break;
		case HW_ST_NONE:
		default:
			type = "";
			break;
	}

	filecfg_iprintf("sensor \"%s\" {\n", sensor->name);
	filecfg_ilevel_inc();
	filecfg_iprintf("sid %d;\n", sensor->set.sid);
	filecfg_iprintf("type \"%s\";\n", type);
	if (FCD_Exhaustive || sensor->set.offset)
		filecfg_iprintf("offset %.1f;\n", temp_to_deltaK(sensor->set.offset));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Parse a hardware sensor from config.
 * @param priv unused
 * @param node the configuration node
 * @param sensor an allocated sensor structure which will be populated according to parsed configuration
 * @return exec status
 */
int hw_lib_filecfg_sensor_parse(const void * restrict const priv, const struct s_filecfg_parser_node * const node, struct s_hw_sensor * const sensor)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sid", true, NULL, NULL, },
		{ NODESTR, "type", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "offset", false, NULL, NULL, },
	};
	const char * sensor_stype;
	float sensor_offset;
	int ret;

	assert(node);
	assert(node->value.stringval);

	if (!sensor)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	sensor->name = node->value.stringval;
	sensor->set.sid = parsers[0].node->value.intval;		// XXX REVIEW DIRECT INDEXING
	sensor_stype = parsers[1].node->value.stringval;
	if (parsers[2].node)
		sensor_offset = (NODEFLT == parsers[2].node->type) ? parsers[2].node->value.floatval : parsers[2].node->value.intval;
	else
		sensor_offset = 0;

	sensor->set.offset = deltaK_to_temp(sensor_offset);

	// match stype - XXX TODO REWORK
	if (!strcmp("PT1000", sensor_stype))
		sensor->set.type = HW_ST_PT1000;
	else if (!strcmp("NI1000", sensor_stype))
		sensor->set.type = HW_ST_NI1000;
	else {
		filecfg_parser_pr_err(_("Line %d: unknown sensor type \"%s\""), parsers[1].node->lineno, sensor_stype);
		return (-EUNKNOWN);
	}

	return (ret);
}

/**
 * Dump a hardware relay to config.
 * @param relay the relay to dump
 */
void hw_lib_filecfg_relay_dump(const struct s_hw_relay * const relay)
{
	if (!relay->set.configured)
		return;

	filecfg_iprintf("relay \"%s\" {\n", relay->name);
	filecfg_ilevel_inc();
	filecfg_iprintf("rid %d;\n", relay->set.rid);
	filecfg_iprintf("failstate %s;\n", relay->set.failstate ? "on" : "off");
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Parse a hardware relay from config.
 * @param priv unused
 * @param node the configuration node
 * @param relay an allocated relay structure which will be populated according to parsed configuration
 * @return exec status
 */
int hw_lib_filecfg_relay_parse(const void * restrict const priv, const struct s_filecfg_parser_node * const node, struct s_hw_relay * const relay)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "rid", true, NULL, NULL, },
		{ NODEBOL, "failstate", true, NULL, NULL, },
	};
	int ret;

	assert(node);
	assert(node->value.stringval);

	if (!relay)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// return if invalid config

	relay->name = node->value.stringval;
	relay->set.rid = parsers[0].node->value.intval;		// XXX REVIEW DIRECT INDEXING
	relay->set.failstate = parsers[1].node->value.boolval;

	return (ret);
}

/**
 * Set (request) hardware relay state.
 * @param relay the hardware relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated when the hardware is instructed to do so.
 */
int hw_lib_relay_set_state(struct s_hw_relay * const relay, const bool turn_on, const timekeep_t change_delay)
{
	const timekeep_t now = timekeep_now();

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state state request if delay permits
	if (turn_on) {
		if (!relay->run.is_on) {
			if ((now - relay->run.off_since) < change_delay)
				return (change_delay - (now - relay->run.off_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = true;
		}
	}
	else {	// turn off
		if (relay->run.is_on) {
			if ((now - relay->run.on_since) < change_delay)
				return (change_delay - (now - relay->run.on_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = false;
		}
	}

	return (ALL_OK);
}

/**
 * Get (request) hardware relay state.
 * Updates run.state_time and returns current state
 * @param relay the hardware relay to read
 * @return run.is_on
 */
int hw_lib_relay_get_state(struct s_hw_relay * const relay)
{
	const timekeep_t now = timekeep_now();

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (relay->run.is_on);
}

/**
 * Update hardware relay state and accounting.
 * This function is meant to be called immediately before the hardware is updated.
 * It will update the is_on state of the relay as well as the accounting fields,
 * assuming the time of the call reflects the time the actual hardware is updated.
 * @param relay the target relay
 * @param now the current timestamp
 * @return #HW_LIB_RCHTURNON if the relay was previously off and turned on, #HW_LIB_RCHTURNOFF if the relay was previously on and turned off,
 * #HW_LIB_RCHNONE if no state change happened, or negative value for error.
 */
int hw_lib_relay_update(struct s_hw_relay * const relay, const timekeep_t now)
{
	int ret = HW_LIB_RCHNONE;

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state counters at state change
	if (relay->run.turn_on) {	// turn on
		if (!relay->run.is_on) {	// relay is currently off
			relay->run.cycles++;	// increment cycle count
			relay->run.is_on = true;
			relay->run.on_since = now;
			if (relay->run.off_since)
				relay->run.off_tottime += now - relay->run.off_since;
			relay->run.off_since = 0;
			ret = HW_LIB_RCHTURNON;
		}
	}
	else {	// turn off
		if (relay->run.is_on) {	// relay is currently on
			relay->run.is_on = false;
			relay->run.off_since = now;
			if (relay->run.on_since)
				relay->run.on_tottime += now - relay->run.on_since;
			relay->run.on_since = 0;
			ret = HW_LIB_RCHTURNOFF;
		}
	}

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (ret);
}
