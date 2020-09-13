//
//  hw_backends/hw_p1/hw_p1.h
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
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_lib.h"
#include "timekeep.h"

#define RELAY_MAX_ID		14	///< maximum valid relay id

/** driver runtime data */
struct s_hw_p1_pdata {
	struct {
		uint_fast8_t nsamples;		///< number of samples for temperature readout LP filtering
	} set;		///< settings (externally set)
	struct {
		bool initialized;		///< hardware is initialized (init() succeeded)
		bool online;			///< hardware is online (online() succeeded)
		timekeep_t sensors_ftime;	///< sensors fetch time
		timekeep_t last_calib;		///< time of last calibration
		uint_fast16_t calib_nodac;	///< sensor calibration value without dac offset (as an ohm value read)
		uint_fast16_t calib_dac;	///< sensor calibration value with dac offset (as on ohm value read)
		int fwversion;			///< firmware version
	} run;		///< private runtime (internally handled)
	struct s_hw_p1_spi spi;			///< spi runtime
	struct s_hw_p1_lcd lcd;			///< lcd subsystem private data
	struct rwchc_s_settings settings;	///< local copy of hardware settings data
	union rwchc_u_relays relays;		///< local copy of hardware relays data
	union rwchc_u_periphs peripherals;	///< local copy of hardware peripheral data
	rwchc_sensor_t sensors[RWCHC_NTSENSORS];///< local copy of hardware sensors data
	struct s_hw_sensor Sensors[RWCHC_NTSENSORS];	///< software view of physical sensors
	struct s_hw_relay Relays[RELAY_MAX_ID];	///< software view of physical relays
	uint_fast8_t scount[RWCHC_NTSENSORS];	///< counter for decimation
};

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
