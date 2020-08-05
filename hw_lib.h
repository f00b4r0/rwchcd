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
#include "timekeep.h"
#include "filecfg_parser.h"

#define HW_LIB_RCHNONE		0x00	///< no change
#define HW_LIB_RCHTURNON	0x01	///< turn on
#define HW_LIB_RCHTURNOFF	0x02	///< turn off

typedef float ohm_to_celsius_ft(const uint_fast16_t);	///< ohm-to-celsius function prototype

/** valid types of temperature sensors */
enum e_hw_stype {
	HW_ST_NONE = 0,	///< No type, misconfiguration
	HW_ST_PT1000,	///< PT1000 sensor. Config `PT1000`
	HW_ST_NI1000,	///< NI1000 sensor. Config `NI1000`
	/*	ST_PT100,
	 ST_LGNI1000, */
};

/** software representation of a hardware sensor */
struct s_hw_sensor {
	struct {
		bool configured;	///< sensor is configured
		sid_t sid;		///< sensor id, valid iff >0
		enum e_hw_stype type;	///< sensor type
		temp_t offset;		///< sensor value offset
	} set;		///< settings (externally set)
	struct {
		temp_t value;		///< sensor current temperature value (offset applied)
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the sensor
};

/** software representation of a hardware relay */
struct s_hw_relay {
	struct {
		bool configured;	///< true if properly configured
		bool failstate;		///< default state assumed by hardware in failsafe mode
		rid_t rid;		///< relay id, valid iff >0
	} set;		///< settings (externally set)
	struct {
		bool turn_on;		///< state requested by software
		bool is_on;		///< current hardware active state
		timekeep_t state_since;	///< last time state changed
		timekeep_t state_time;	///< time spent in current state
		uint_fast32_t on_totsecs;	///< total seconds spent in on state since epoch (updated at state change only)
		uint_fast32_t off_totsecs;	///< total seconds spent in off state since epoch (updated at state change only)
		uint_fast32_t cycles;	///< number of power cycles since epoch
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the relay
};

ohm_to_celsius_ft * hw_lib_sensor_o_to_c(const enum e_hw_stype stype);

void hw_lib_filecfg_sensor_dump(const struct s_hw_sensor * const sensor);
int hw_lib_filecfg_sensor_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
void hw_lib_filecfg_relay_dump(const struct s_hw_relay * const relay);
int hw_lib_filecfg_relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

int hw_lib_relay_set_state(struct s_hw_relay * const relay, const bool turn_on, const timekeep_t change_delay);
int hw_lib_relay_get_state(struct s_hw_relay * const relay);
int hw_lib_relay_update(struct s_hw_relay * const relay, const timekeep_t now);
void hw_lib_relay_restore(struct s_hw_relay * restrict const rdest, const struct s_hw_relay * restrict const rsrc);

#endif /* hw_lib_h */
