//
//  hw_lib.c
//  rwchcd
//
//  (C) 2019-2020 Thibaut VARENE
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
#include <string.h>
#include <stdlib.h>	// free

#include "filecfg_dump.h"
#include "lib.h"
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
	return ((-R0*A + sqrtf(R0*R0*A*A - 4.0F*R0*B*(R0 - (float)ohm))) / (2.0F*R0*B));
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
	const float A = 5.485e-3F;
	const float B = 6.650e-6F;

	return (quadratic_cvd(R0, A, B, ohm));
}

/**
 * Return a sensor ohm to celsius converter callback based on sensor type.
 * @param stype the sensor type identifier
 * @return correct function pointer for sensor type or NULL if invalid type
 */
__attribute__ ((pure)) ohm_to_celsius_ft * hw_lib_sensor_o_to_c(const struct s_hw_sensor * restrict const sensor)
{
	assert(sensor);
	switch (sensor->set.type) {
		case HW_ST_PT1000:
			return (pt1000_ohm_to_celsius);
		case HW_ST_NI1000:
			return (ni1000_ohm_to_celsius);
		case HW_ST_NONE:
		default:
			return (NULL);
	}
}

static const char * const hw_lib_sensor_type_str[] = {
	[HW_ST_NONE] = "",
	[HW_ST_PT1000] = "PT1000",
	[HW_ST_NI1000] = "NI1000",
};

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
		case HW_ST_NI1000:
			type = hw_lib_sensor_type_str[sensor->set.type];
			break;
		case HW_ST_NONE:
		default:
			type = hw_lib_sensor_type_str[HW_ST_NONE];
			break;
	}

	filecfg_iprintf("sensor \"%s\" {\n", sensor->name);
	filecfg_ilevel_inc();
	filecfg_iprintf("sid %d;\n", sensor->set.sid);
	filecfg_dump_nodestr("type", type);
	if (FCD_Exhaustive || sensor->set.offset)
		filecfg_dump_deltaK("offset", sensor->set.offset);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Parse a hardware sensor from config.
 * @param priv an allocated sensor structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
int hw_lib_filecfg_sensor_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sid", true, NULL, NULL, },
		{ NODESTR, "type", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "offset", false, NULL, NULL, },
	};
	struct s_hw_sensor * const sensor = priv;
	const char * sensor_stype;
	float sensor_offset;
	unsigned int i;
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
		sensor_offset = (NODEFLT == parsers[2].node->type) ? parsers[2].node->value.floatval : (float)parsers[2].node->value.intval;
	else
		sensor_offset = 0;

	sensor->set.offset = deltaK_to_temp(sensor_offset);

	// match stype
	for (i = 0; i < ARRAY_SIZE(hw_lib_sensor_type_str); i++) {
		if (!strcmp(hw_lib_sensor_type_str[i], sensor_stype)) {
			sensor->set.type = i;
			return (ret);
		}
	}

	// matching failed
	filecfg_parser_pr_err(_("Line %d: unknown sensor type \"%s\""), parsers[1].node->lineno, sensor_stype);
	return (-EUNKNOWN);
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
 * @param priv an allocated relay structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
int hw_lib_filecfg_relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "rid", true, NULL, NULL, },
		{ NODEBOL, "failstate", true, NULL, NULL, },
	};
	struct s_hw_relay * const relay = priv;
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
 * Duplicate a hardware sensor from source.
 * This function is typically intended to be used in a setup process post config parsing.
 * @param snew an allocated sensor structure which will be populated according #ssrc
 * @param ssrc the source sensor whose configuration will be copied
 * @return exec status
 */
int hw_lib_sensor_setup_copy(struct s_hw_sensor * restrict const snew, const struct s_hw_sensor * restrict const ssrc)
{
	char * str;

	assert(snew && ssrc);

	// ensure valid type
	if (!hw_lib_sensor_o_to_c(ssrc))
		return (-EINVALID);

	str = strdup(ssrc->name);
	if (!str)
		return(-EOOM);

	snew->name = str;
	snew->set.sid = ssrc->set.sid;
	snew->set.type = ssrc->set.type;
	snew->set.offset = ssrc->set.offset;
	snew->set.configured = true;

	return (ALL_OK);
}

/**
 * Clone sensor temperature.
 * This function checks that the designated sensor is properly configured in software.
 * Finally, if parameter #tclone is non-null, the temperature of the sensor
 * is copied, with configuration offset applied.
 * @param sensor target sensor
 * @param tclone optional location to copy the sensor temperature.
 * @param adjust result will be offset-adjusted if true
 * @return exec status
 */
int hw_lib_sensor_clone_temp(const struct s_hw_sensor * restrict const sensor, temp_t * const tclone, bool adjust)
{
	int ret;
	temp_t temp;

	assert(sensor);

	if (!sensor->set.configured)
		return (-ENOTCONFIGURED);

	temp = sensor->run.value;

	if (tclone)
		*tclone = temp + (adjust ? sensor->set.offset : 0);

	switch (temp) {
		case TEMPUNSET:
			ret = -ESENSORINVAL;
			break;
		case TEMPSHORT:
			ret = -ESENSORSHORT;
			break;
		case TEMPDISCON:
			ret = -ESENSORDISCON;
			break;
		case TEMPINVALID:
			ret = -EINVALID;
			break;
		default:
			ret = ALL_OK;
			break;
	}

	return (ret);
}

/**
 * Store raw sensor value.
 * @param sensor the target sensor
 * @param temp the value to store
 * @return ALL_OK
 * @warning no sanity check is performed
 */
int hw_lib_sensor_set_temp(struct s_hw_sensor * restrict const sensor, const temp_t temp)
{
	assert(sensor);

	sensor->run.value = temp;

	return (ALL_OK);
}

/**
 * Get sensor name.
 * @param relay the relay to get the name
 * @return relay name if available, NULL otherwise
 */
const char * hw_lib_sensor_get_name(const struct s_hw_sensor * restrict const sensor)
{
	assert(sensor);

	if (sensor->set.configured)
		return (sensor->name);
	else
		return NULL;
}

/**
 * Discard a sensor.
 * @param sensor the sensor to trash
 * @warning not thread safe (should be only used in exit routine)
 */
void hw_lib_sensor_discard(struct s_hw_sensor * const sensor)
{
	free((void *)sensor->name);

	memset(sensor, 0x00, sizeof(*sensor));
}

/**
 * Duplicate a hardware relay from source.
 * This function is typically intended to be used in a setup process post config parsing.
 * @param rnew an allocated relay structure which will be populated according #rsrc
 * @param rsrc the source relay whose configuration will be copied
 * @return exec status
 * @note sets relay's run.off_since
 */
int hw_lib_relay_setup_copy(struct s_hw_relay * restrict const rnew, const struct s_hw_relay * restrict const rsrc)
{
	char * str;

	assert(rnew && rsrc);

	str = strdup(rsrc->name);
	if (!str)
		return(-EOOM);

	rnew->name = str;

	// register failover state
	rnew->set.failstate = rsrc->set.failstate;
	rnew->set.rid = rsrc->set.rid;

	rnew->run.state_since = timekeep_now();	// relay is by definition OFF since "now"

	rnew->set.configured = true;

	return (ALL_OK);
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

	if (unlikely(!relay->set.configured))
		return (-ENOTCONFIGURED);

	// update state state request if necessary and delay permits
	if (turn_on != relay->run.is_on) {
		if ((now - relay->run.state_since) < change_delay)
			return ((int)(change_delay - (now - relay->run.state_since)));	// < INT_MAX - don't do anything if previous state hasn't been held long enough - return remaining time

		relay->run.turn_on = turn_on;
	}

	return (ALL_OK);
}

/**
 * Get (request) hardware relay state.
 * Returns current state
 * @param relay the hardware relay to read
 * @return run.is_on or error
 * @note after successful call to hw_lib_relay_update() this function is guaranteed not to fail.
 */
int hw_lib_relay_get_state(const struct s_hw_relay * const relay)
{
	if (unlikely(!relay->set.configured))
		return (-ENOTCONFIGURED);

	return (relay->run.is_on);
}

/**
 * Update hardware relay state and accounting.
 * This function is meant to be called immediately before the hardware is updated.
 * It will update the is_on state of the relay as well as the accounting fields,
 * assuming the #now parameter reflects the time the actual hardware is updated.
 * @param relay the target relay
 * @param now the current timestamp
 * @return #HW_LIB_RCHTURNON if the relay was previously off and turned on,
 * #HW_LIB_RCHTURNOFF if the relay was previously on and turned off,
 * #HW_LIB_RCHNONE if no state change happened, or negative value for error.
 */
int hw_lib_relay_update(struct s_hw_relay * const relay, const timekeep_t now)
{
	int ret = HW_LIB_RCHNONE;

	if (unlikely(!relay->set.configured))
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = now - relay->run.state_since;

	// update state counters at state change
	if (relay->run.turn_on != relay->run.is_on) {
		if (!relay->run.is_on) {	// relay is currently off => turn on
			relay->run.cycles++;	// increment cycle count
			relay->run.off_totsecs += (unsigned)timekeep_tk_to_sec(relay->run.state_time);
			ret = HW_LIB_RCHTURNON;
		}
		else {				// relay is currently on => turn off
			relay->run.on_totsecs += (unsigned)timekeep_tk_to_sec(relay->run.state_time);
			ret = HW_LIB_RCHTURNOFF;
		}

		relay->run.is_on = relay->run.turn_on;
		relay->run.state_since = now;
		relay->run.state_time = 0;
	}

	return (ret);
}

/**
 * Get relay name.
 * @param relay the relay to get the name
 * @return relay name if available, NULL otherwise
 */
const char * hw_lib_relay_get_name(const struct s_hw_relay * restrict const relay)
{
	assert(relay);

	if (relay->set.configured)
		return (relay->name);
	else
		return NULL;
}

/**
 * Routine to restore relevant data for hardware relays state from permanent storage.
 * Restores cycles and on/off total time counts.
 * @param rdest target data structure
 * @param rsrc source data structure (e.g. from permanent storage)
 */
void hw_lib_relay_restore(struct s_hw_relay * restrict const rdest, const struct s_hw_relay * restrict const rsrc)
{
	assert(rdest && rsrc);
	assert(!rdest->run.is_on);

	// handle saved state
	if (rsrc->run.is_on)
		rdest->run.on_totsecs += (unsigned)timekeep_tk_to_sec(rsrc->run.state_time);
	else
		rdest->run.off_totsecs += (unsigned)timekeep_tk_to_sec(rsrc->run.state_time);
	rdest->run.state_since = timekeep_now();
	rdest->run.on_totsecs += rsrc->run.on_totsecs;
	rdest->run.off_totsecs += rsrc->run.off_totsecs;
	rdest->run.cycles += rsrc->run.cycles;
}

/**
 * Discard a relay.
 * @param relay the relay to trash
 * @warning not thread safe (should be only used in exit routine)
 */
void hw_lib_relay_discard(struct s_hw_relay * const relay)
{
	free((void *)relay->name);

	memset(relay, 0x00, sizeof(*relay));
}
