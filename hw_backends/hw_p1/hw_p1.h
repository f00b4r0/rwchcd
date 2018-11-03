//
//  hw_p1.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
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

#define HWP1_LOG_INTVL_TEMPS	60	///< log temperatures every X seconds

/** valid types of temperature sensors */
enum e_hw_p1_stype {
	ST_PT1000,
	ST_NI1000,
	/*	ST_PT100,
	 ST_LGNI1000, */
};

/** software representation of a hardware relay */
struct s_hw_p1_relay {
	struct {
		bool configured;	///< true if properly configured
		bool failstate;		///< default state assumed by hardware in failsafe mode
	} set;		///< settings (externally set)
	struct {
		bool turn_on;		///< state requested by software
		bool is_on;		///< current hardware active state
		time_t on_since;	///< last time on state was triggered, 0 if off
		time_t off_since;	///< last time off state was triggered, 0 if on
		time_t state_time;	///< time spent in current state
		time_t on_tottime;	///< total time spent in on state since system start (updated at state change only)
		time_t off_tottime;	///< total time spent in off state since system start (updated at state change only)
		uint_fast32_t cycles;	///< number of power cycles
	} run;		///< private runtime (internally handled)
	char * restrict name;		///< @b unique user-defined name for the relay
};

typedef float ohm_to_celsius_ft(const uint_fast16_t);	///< ohm-to-celsius function prototype

struct s_hw_p1_sensor {
	struct {
		bool configured;	///< sensor is configured
		enum e_hw_p1_stype type;///< sensor type
		temp_t offset;		///< sensor value offset
	} set;		///< settings (externally set)
	struct {
		temp_t value;		///< sensor current temperature value (offset applied)
	} run;		///< private runtime (internally handled)
	ohm_to_celsius_ft * ohm_to_celsius;
	char * restrict name;		///< @b unique user-defined name for the sensor
};

#define RELAY_MAX_ID		14	///< maximum valid relay id

/** driver runtime data */
struct s_hw_p1_pdata {
	struct {
		uint_fast8_t nsamples;		///< number of samples for temperature readout LP filtering
	} set;		///< settings (externally set)
	struct {
		bool initialized;		///< hardware is initialized (init() succeeded)
		bool online;			///< hardware is online (online() succeeded)
		time_t sensors_ftime;		///< sensors fetch time
		time_t last_calib;		///< time of last calibration
		float calib_nodac;		///< sensor calibration value without dac offset
		float calib_dac;		///< sensor calibration value with dac offset
		int fwversion;			///< firmware version
	} run;		///< private runtime (internally handled)
	struct rwchc_s_settings settings;	///< local copy of hardware settings data
	union rwchc_u_relays relays;		///< local copy of hardware relays data
	union rwchc_u_periphs peripherals;	///< local copy of hardware peripheral data
	rwchc_sensor_t sensors[RWCHC_NTSENSORS];///< local copy of hardware sensors data
	pthread_rwlock_t Sensors_rwlock;	///< For thread safe access to ::Sensors.value
	struct s_hw_p1_sensor Sensors[RWCHC_NTSENSORS];	///< software view of physical sensors
	struct s_hw_p1_relay Relays[RELAY_MAX_ID];	///< software view of physical relays
};

extern struct s_hw_p1_pdata Hardware;

int hw_p1_hwconfig_commit(void);
int hw_p1_calibrate(void);
int hw_p1_save_relays(void);
int hw_p1_restore_relays(void);
int hw_p1_sensors_read(void);
int hw_p1_rwchcrelays_write(void);
int hw_p1_rwchcperiphs_write(void);
int hw_p1_rwchcperiphs_read(void);

int hw_p1_async_log_temps(void);

int hw_p1_sid_by_name(const char * const name);
int hw_p1_rid_by_name(const char * const name);

ohm_to_celsius_ft * hw_p1_sensor_o_to_c(const enum e_hw_p1_stype type);

#endif /* rwchcd_hw_p1_h */
