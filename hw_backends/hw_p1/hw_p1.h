//
//  hw_backends/hw_p1/hw_p1.h
//  rwchcd
//
//  (C) 2016,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 driver API.
 */

#ifndef rwchcd_hw_p1_h
#define rwchcd_hw_p1_h

#include "rwchcd.h"
#include "rwchc_export.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_backends/hw_lib.h"
#include "timekeep.h"

#define RELAY_MAX_ID		14	///< maximum valid relay id

/** valid types of temperature sensors */
enum e_hw_p1_stype {
	HW_P1_ST_NONE = 0,	///< No type, misconfiguration
	HW_P1_ST_PT1000,	///< PT1000 sensor. Config `PT1000`
	HW_P1_ST_NI1000,	///< NI1000 sensor. Config `NI1000`
	/*	ST_PT100,
	 ST_LGNI1000, */
};

/** software representation of a hardware sensor. */
struct s_hw_p1_sensor {
	struct {
		bool configured;	///< sensor is configured
		uint_fast8_t channel;	///< sensor channel, numbered from 1 to 15
		enum e_hw_p1_stype type;///< sensor type
		tempdiff_t offset;	///< sensor value offset
	} set;		///< settings (externally set)
	struct {
		_Atomic temp_t value;	///< sensor current temperature value
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the sensor
};

/** software representation of a hardware relay. */
struct s_hw_p1_relay {
	struct {
		bool configured;	///< true if properly configured
		bool failstate;		///< default state assumed by hardware in failsafe mode
		uint_fast8_t channel;	///< relay channel, numbered from 1 to 14 (R1 and R2 are 13 and 14)
	} set;		///< settings (externally set)
	struct {
		bool turn_on;		///< state requested by software
		bool is_on;		///< current hardware active state
		timekeep_t state_since;	///< last time state changed
		/* The following elements are only accessed within _relay_update()
		 * and _relay_restore() which can never happen concurrently */
		timekeep_t state_time;	///< time spent in current state
		uint_fast32_t on_totsecs;	///< total seconds spent in on state since epoch (updated at state change only)
		uint_fast32_t off_totsecs;	///< total seconds spent in off state since epoch (updated at state change only)
		uint_fast32_t cycles;	///< number of power cycles since epoch
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the relay
};

/** driver runtime data */
struct s_hw_p1_pdata {
	struct {
		uint_fast8_t nsamples;		///< number of samples for temperature readout LP filtering
	} set;		///< settings (externally set)
	struct {
		bool initialized;		///< hardware is initialized (setup() succeeded)
		bool online;			///< hardware is online (online() succeeded)
		timekeep_t sensors_ftime;	///< sensors fetch time
		timekeep_t last_calib;		///< time of last calibration
		uint_fast16_t calib_nodac;	///< sensor calibration value without dac offset (as an ohm value read)
		uint_fast16_t calib_dac;	///< sensor calibration value with dac offset (as on ohm value read)
		int fwversion;			///< firmware version
	} run;		///< private runtime (internally handled)
	struct rwchc_s_settings settings;	///< local copy of hardware settings data
	union rwchc_u_relays relays;		///< local copy of hardware relays data
	union rwchc_u_periphs peripherals;	///< local copy of hardware peripheral data
	struct s_hw_p1_spi spi;			///< spi runtime
	struct s_hw_p1_lcd lcd;			///< lcd subsystem private data
	rwchc_sensor_t sensors[RWCHC_NTSENSORS];///< local copy of hardware sensors data
	struct s_hw_p1_sensor Sensors[RWCHC_NTSENSORS];	///< software view of physical sensors
	uint_fast8_t scount[RWCHC_NTSENSORS];	///< counter for decimation
	struct s_hw_p1_relay Relays[RELAY_MAX_ID];	///< software view of physical relays
};

typedef float ohm_to_celsius_ft(const uint_fast16_t);	///< ohm-to-celsius function prototype

ohm_to_celsius_ft * hw_p1_sensor_o_to_c(const struct s_hw_p1_sensor * restrict const sensor);

int hw_p1_hwconfig_commit(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_calibrate(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_save_relays(const struct s_hw_p1_pdata * restrict const hw);
int hw_p1_restore_relays(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_sensors_read(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_rwchcrelays_write(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_rwchcperiphs_write(struct s_hw_p1_pdata * restrict const hw);
int hw_p1_rwchcperiphs_read(struct s_hw_p1_pdata * restrict const hw);

int hw_p1_sid_by_name(const struct s_hw_p1_pdata * restrict const hw, const char * restrict const name);
int hw_p1_rid_by_name(const struct s_hw_p1_pdata * restrict const hw, const char * restrict const name);

#endif /* rwchcd_hw_p1_h */
