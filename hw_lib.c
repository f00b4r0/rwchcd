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
