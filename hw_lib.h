//
//  hw_lib.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware-related functions library API.
 */

#ifndef hw_lib_h
#define hw_lib_h

#include "rwchcd.h"
#include "filecfg_parser.h"

typedef float ohm_to_celsius_ft(const uint_fast16_t);	///< ohm-to-celsius function prototype

/** valid types of temperature sensors */
enum e_hw_stype {
	HW_ST_NONE = 0,	///< No type, misconfiguration
	HW_ST_PT1000,	///< PT1000 sensor. Config "PT1000"
	HW_ST_NI1000,	///< NI1000 sensor. Config "NI1000"
	/*	ST_PT100,
	 ST_LGNI1000, */
};

/** software representation of a hardware sensor */
struct s_hw_sensor {
	struct {
		bool configured;	///< sensor is configured
		sid_t sid;		///< sensor id
		enum e_hw_stype type;	///< sensor type
		temp_t offset;		///< sensor value offset
	} set;		///< settings (externally set)
	struct {
		temp_t value;		///< sensor current temperature value (offset applied)
	} run;		///< private runtime (internally handled)
	char * restrict name;		///< @b unique (per backend) user-defined name for the sensor
};

ohm_to_celsius_ft * hw_lib_sensor_o_to_c(const enum e_hw_stype stype);

void hw_lib_filecfg_sensor_dump(const struct s_hw_sensor * const sensor);
int hw_lib_filecfg_sensor_parse(const void * restrict const priv, const struct s_filecfg_parser_node * const node, struct s_hw_sensor * const sensor);

#endif /* hw_lib_h */
