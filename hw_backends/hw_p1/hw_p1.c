//
//  hw_backends/hw_p1/hw_p1.c
//  rwchcd
//
//  (C) 2016-2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 driver implementation.
 * @note This driver should NOT be considered a good coding example, it is
 * heavily tailored to the context of a single prototype hardware controller
 * connected to a RaspberryPi GPIO header, and as such contains hardcoded values,
 * which is deemed acceptable in this particular context but should otherwise be frowned upon.
 * @note To build this driver, the `rwchc_export.h` header from the hardware's
 * firmware code is necessary.
 */

#include <time.h>	// time
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset/strdup
#include <assert.h>
#include <stdatomic.h>

#include "lib.h"
#include "storage.h"
#include "log/log.h"
#include "alarms.h"
#include "hw_backends/hw_lib.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1.h"

#define HW_P1_RCHNONE		0x00	///< no change
#define HW_P1_RCHTURNON		0x01	///< turn on
#define HW_P1_RCHTURNOFF	0x02	///< turn off

static const storage_version_t Hardware_sversion = 3;

/**
 * Convert sensor value to actual resistance.
 * 1 LSB = (2 * Vref / Gain ) / 2^16 = +FS / 2^15
 * @param raw the raw sensor value
 * @return the resistance value
 */
static inline res_t sensor_to_res(const rwchc_sensor_t raw)
{
	res_t value;

	value = raw * RWCHC_CALIB_OHM / RWCHC_ADC_GAIN;
	value *= RES_OHMMULT;
	value >>= RWCHC_ADC_FSBITS-1;

	return (value);
}

/**
 * Raise an alarm for a specific sensor.
 * This function raises an alarm if the sensor's temperature is invalid.
 * @param sensor target sensor
 * @param error sensor error
 * @return exec status
 */
static int sensor_alarm(const struct s_hw_p1_sensor * const sensor, const int error)
{
	const char * restrict fail, * restrict name = NULL;
	const uint_fast8_t channel = sensor->set.channel;
	int ret;

	switch (error) {
		case -ESENSORSHORT:
			fail = _("shorted");
			name = sensor->name;
			break;
		case -ESENSORDISCON:
			fail = _("disconnected");
			name = sensor->name;
			break;
		case -ESENSORINVAL:
			fail = _("invalid");
			break;
		default:
			fail = _("error");
			break;
	}

	ret = alarms_raise(error, _("HWP1: sensor \"%s\" (%d): %s"), name, channel, fail);

	return (ret);
}

/**
 * Process raw sensor data.
 * Flag and raise alarm if value is out of #RWCHCD_TEMPMIN and #RWCHCD_TEMPMAX bounds.
 * @param hw HW P1 private data
 */
static void hw_p1_parse_temps(struct s_hw_p1_pdata * restrict const hw)
{
	struct s_hw_p1_sensor * sensor;
	uint_fast8_t i;
	res_t res;
	temp_t previous, current;

	assert(hw->run.initialized);

	for (i = 0; i < hw->run.nsensors; i++) {
		sensor = &hw->Sensors[i];
		if (!sensor->set.configured) {
			aser(&sensor->run.value, TEMPUNSET);
			continue;
		}

		res = sensor_to_res(hw->sensors[sensor->set.channel-1]);

		// R0 hardcoded: HWP1 only supports R0 = 1000 ohms
		current = celsius_to_temp(hw_lib_rtd_res_to_celsius(sensor->set.type, hw_lib_ohm_to_res(1000), res));
		previous = aler(&sensor->run.value);

		if (current <= RWCHCD_TEMPMIN) {
			// delay by hardcoded 5 samples
			if (hw->scount[i] < 5) {
				hw->scount[i]++;
				dbgmsg(1, 1, "delaying sensor %d short, samples ignored: %d", i+1, hw->scount[i]);
			}
			else {
				aser(&sensor->run.value, TEMPSHORT);
				sensor_alarm(sensor, -ESENSORSHORT);
			}
		}
		else if (current >= RWCHCD_TEMPMAX) {
			// delay by hardcoded 5 samples
			if (hw->scount[i] < 5) {
				hw->scount[i]++;
				dbgmsg(1, 1, "delaying sensor %d disconnect, samples ignored: %d", i+1, hw->scount[i]);
			}
			else {
				aser(&sensor->run.value, TEMPDISCON);
				sensor_alarm(sensor, -ESENSORDISCON);
			}
		}
		// init or recovery
		else if (previous <= TEMPINVALID) {
			hw->scount[i] = 0;
			aser(&sensor->run.value, current);
		}
		// normal operation
		else {
			// decimate large changes to work around measurement instability. Hardcoded 4C / 5 samples (i.e. ~5 seconds) max
			if (((current < (previous - deltaK_to_temp(4))) || (current > (previous + deltaK_to_temp(4)))) && hw->scount[i]++ < 5)
				dbgmsg(1, 1, "decimating sensor %d value, samples ignored: %d", i+1, hw->scount[i]);
			else {
				// apply LP filter - ensure we only apply filtering on valid temps
				// scount[i]+1 will ensure that if we exceed decimation threshold, the new value "weighs in" immediately
				aser(&sensor->run.value, temp_expw_mavg(previous, current, hw->set.nsamples, hw->scount[i]+1));
				hw->scount[i] = 0;
			}
		}
	}
}

/**
 * Save hardware relays state to permanent storage.
 * @param hw HW P1 private data
 * @return exec status
 * @bug hardcoded identifier will collide if multiple instances.
 */
int hw_p1_save_relays(const struct s_hw_p1_pdata * restrict const hw)
{
	assert(hw->run.online);
	return (storage_dump("hw_p1_relays", &Hardware_sversion, hw->Relays, sizeof(hw->Relays)));
}

/**
 * Restore hardware relays state from permanent storage.
 * Restores cycles and on/off total time counts for all relays.
 * @param hw HW P1 private data
 * @return exec status
 * @bug hardcoded identifier will collide if multiple instances.
 * @note Each relay is "restored" in OFF state (due to initialization in
 * hw_p1_setup_new()).
 */
int hw_p1_restore_relays(struct s_hw_p1_pdata * restrict const hw)
{
	const timekeep_t now = timekeep_now();
	static typeof (hw->Relays) blob;
	storage_version_t sversion;
	typeof(&hw->Relays[0]) blobptr = (typeof(blobptr))&blob;
	unsigned int i;
	int ret;
	
	// try to restore key elements of hardware
	ret = storage_fetch("hw_p1_relays", &sversion, blob, sizeof(blob));
	if (ALL_OK == ret) {
		if (Hardware_sversion != sversion)
			return (-EMISMATCH);

		for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
			// handle saved state (see @note)
			if (blobptr->run.is_on)
				hw->Relays[i].run.on_totsecs += (unsigned)timekeep_tk_to_sec(blobptr->run.state_time);
			else
				hw->Relays[i].run.off_totsecs += (unsigned)timekeep_tk_to_sec(blobptr->run.state_time);
			hw->Relays[i].run.state_since = now;
			hw->Relays[i].run.on_totsecs += blobptr->run.on_totsecs;
			hw->Relays[i].run.off_totsecs += blobptr->run.off_totsecs;
			hw->Relays[i].run.cycles += blobptr->run.cycles;
			blobptr++;
		}
		dbgmsg(1, 1, "Hardware relay state restored");
	}

	return (ret);
}

/**
 * Update internal relay system based on target state.
 * This function takes an incremental physical relay id and adjusts
 * the internal hardware data structure based on the desired relay
 * state.
 * @param rWCHC_relays target internal relay system
 * @param id target relay id (from 0)
 * @param state target state
 */
__attribute__((always_inline)) static inline void rwchc_relay_set(union rwchc_u_relays * const rWCHC_relays, const uint_fast8_t id, const bool state)
{
	uint_fast8_t rid = id;

	// adapt relay id XXX REVISIT
	if (rid > 6)
		rid++;	// skip the hole

	// set state for triac control
	if (state)
		setbit(rWCHC_relays->ALL, rid);
	else
		clrbit(rWCHC_relays->ALL, rid);
}

/**
 * Prepare hardware settings 'deffail' data based on Relays configuration.
 * @param hw HW P1 private data
 */
static void hw_p1_rwchcsettings_deffail(struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast8_t i;

	// start clean
	hw->settings.deffail.ALL = 0;

	// update each known hardware relay
	for (i = 0; i < ARRAY_SIZE(hw->Relays); i++) {
		if (!hw->Relays[i].set.configured)
			continue;

		// update internal structure
		rwchc_relay_set(&hw->settings.deffail, i, hw->Relays[i].set.failstate);
	}
}

static void hw_p1_rwchcsettings_actsens(struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast8_t i;

	// start clean
	hw->settings.actsens.ALL = 0;

	// update each known hardware relay
	for (i = 0; i < ARRAY_SIZE(hw->Sensors); i++) {
		if (!hw->Sensors[i].set.configured)
			continue;

		// update internal structure
		switch (hw->Sensors[i].set.channel) {
			case 1:
				hw->settings.actsens.S1 = 1;
				break;
			case 2:
				hw->settings.actsens.S2 = 1;
				break;
			case 3:
				hw->settings.actsens.S3 = 1;
				break;
			case 4:
				hw->settings.actsens.S4 = 1;
				break;
			case 5:
				hw->settings.actsens.S5 = 1;
				break;
			case 6:
				hw->settings.actsens.S6 = 1;
				break;
			case 7:
				hw->settings.actsens.S7 = 1;
				break;
			case 8:
				hw->settings.actsens.S8 = 1;
				break;
			case 9:
				hw->settings.actsens.S9 = 1;
				break;
			case 10:
				hw->settings.actsens.S10 = 1;
				break;
			case 11:
				hw->settings.actsens.S11 = 1;
				break;
			case 12:
				hw->settings.actsens.S12 = 1;
				break;
			case 13:
				hw->settings.actsens.S13 = 1;
				break;
			case 14:
				hw->settings.actsens.S14 = 1;
				break;
			default:
				dbgerr("Invalid sensor channel: %d", hw->Sensors[i].set.channel);
				break;
		}
	}
}

/**
 * Commit hardware config to hardware.
 * @note overwrites all hardware settings.
 * @param hw HW P1 private data
 * @return exec status
 */
int hw_p1_hwconfig_commit(struct s_hw_p1_pdata * restrict const hw)
{
	typeof(hw->settings) hw_set;
	int ret;
	
	assert(hw->run.initialized);

	// prepare hardware settings.deffail and active sensor data
	hw_p1_rwchcsettings_deffail(hw);
	hw_p1_rwchcsettings_actsens(hw);
	
	// grab current config from the hardware
	ret = hw_p1_spi_settings_r(&hw->spi, &hw_set);
	if (ret)
		goto out;
	
	if (!memcmp(&hw_set, &(hw->settings), sizeof(hw_set)))
		return (ALL_OK); // don't wear flash down if unnecessary
	
	// commit hardware config
	ret = hw_p1_spi_settings_w(&hw->spi, &(hw->settings));
	if (ret)
		goto out;

	// check that the data is correct on target
	ret = hw_p1_spi_settings_r(&hw->spi, &hw_set);
	if (ret)
		goto out;

	if (memcmp(&hw_set, &(hw->settings), sizeof(hw->settings))) {
		ret = -EHARDWARE;
		goto out;
	}
	
	// save hardware config
	ret = hw_p1_spi_settings_s(&hw->spi);

	dbgmsg(1, 1, "HW Config saved.");
	
out:
	return (ret);
}

/**
 * Read all temperature sensors.
 * This function will read all sensors into hw->sensors and if no error occurs:
 * - hw->run.sensors_ftime will be updated
 * - Raw values from hw->sensors are processed to atomically update #hw->Sensors
 * otherwise these fields remain unchanged.
 *
 * @param hw HW P1 private data
 * @return exec status
 * @note calling hw_p1_parse_temps() in the success code path is a design choice that
 * ensures a consistent view of system temperatures:
 * either all values are updated coherently or none are.
 */
int hw_p1_sensors_read(struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast8_t sensor;
	int ret = ALL_OK;
	
	assert(hw->run.initialized);

	for (sensor = 0; sensor < ARRAY_SIZE(hw->sensors); sensor++) {
		ret = hw_p1_spi_sensor_r(&hw->spi, hw->sensors, sensor);
		if (ret)
			goto out;
	}

	hw_p1_parse_temps(hw);

	hw->run.sensors_ftime = timekeep_now();

out:
	return (ret);
}

/**
 * Update hardware relay state and accounting.
 * This function is meant to be called immediately before the hardware is updated.
 * It will update the is_on state of the relay as well as the accounting fields,
 * assuming the #now parameter reflects the time the actual hardware is updated.
 * @param relay the target relay
 * @param now the current timestamp
 * @return #HW_P1_RCHTURNON if the relay was previously off and turned on,
 * #HW_P1_RCHTURNOFF if the relay was previously on and turned off,
 * #HW_P1_RCHNONE if no state change happened, or negative value for error.
 */
static int hw_p1_relay_update(struct s_hw_p1_relay * const relay, const timekeep_t now)
{
	int ret = HW_P1_RCHNONE;

	if (unlikely(!relay->set.configured))
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = now - relay->run.state_since;

	// update state counters at state change
	if (relay->run.turn_on != relay->run.is_on) {
		if (!relay->run.is_on) {	// relay is currently off => turn on
			relay->run.cycles++;	// increment cycle count
			relay->run.off_totsecs += (unsigned)timekeep_tk_to_sec(relay->run.state_time);
			ret = HW_P1_RCHTURNON;
		}
		else {				// relay is currently on => turn off
			relay->run.on_totsecs += (unsigned)timekeep_tk_to_sec(relay->run.state_time);
			ret = HW_P1_RCHTURNOFF;
		}

		relay->run.is_on = relay->run.turn_on;
		relay->run.state_since = now;
		relay->run.state_time = 0;
	}

	return (ret);
}

/**
 * Write all relays
 * This function updates all known hardware relays according to their desired turn_on
 * state. This function also does time and cycle accounting for the relays.
 * @note non-configured hardware relays are turned off.
 * @param hw HW P1 private data
 * @return status
 */
__attribute__((warn_unused_result)) int hw_p1_rwchcrelays_write(struct s_hw_p1_pdata * restrict const hw)
{
	struct s_hw_p1_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const timekeep_t now = timekeep_now();	// we assume the whole thing will take much less than a second
	uint_fast8_t i;
	int ret = -EGENERIC, chflags = HW_P1_RCHNONE;

	assert(hw->run.online);

	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
		relay = &hw->Relays[i];

		// perform relay accounting
		ret = hw_p1_relay_update(relay, now);
		if (ret < 0)
			continue;
		else
			chflags |= ret;

		// update internal structure
		rwchc_relay_set(&rWCHC_relays, i, (bool)relay->run.is_on);
	}

	// save/log relays state if there was a change
	if (chflags) {
		if (chflags & HW_P1_RCHTURNOFF) {	// only update permanent storage on full cycles (at turn off)
			// XXX there's no real motive to do this besides lowering storage pressure
			ret = hw_p1_save_relays(hw);
			if (ret)
				dbgerr("hw_p1_save failed (%d)", ret);
		}
	}
	
	// send new state to hardware
	ret = hw_p1_spi_relays_w(&hw->spi, &rWCHC_relays);

	// update internal runtime state on success
	if (ALL_OK == ret)
		hw->relays.ALL = rWCHC_relays.ALL;

	return (ret);
}

/**
 * Write all peripherals from internal runtime to hardware
 * @param hw HW P1 private data
 * @return status
 */
__attribute__((warn_unused_result, always_inline)) inline int hw_p1_rwchcperiphs_write(struct s_hw_p1_pdata * restrict const hw)
{
	assert(hw->run.online);
	return (hw_p1_spi_peripherals_w(&hw->spi, &(hw->peripherals)));
}

/**
 * Read all peripherals from hardware into internal runtime
 * @param hw HW P1 private data
 * @return exec status
 */
__attribute__((warn_unused_result, always_inline)) inline int hw_p1_rwchcperiphs_read(struct s_hw_p1_pdata * restrict const hw)
{
	assert(hw->run.online);
	return (hw_p1_spi_peripherals_r(&hw->spi, &(hw->peripherals)));
}

/**
 * Find sensor id by name.
 * @param hw private hw_p1 hardware data
 * @param name name to look for
 * @return -ENOTFOUND if not found, sensor id if found
 */
int hw_p1_sid_by_name(const struct s_hw_p1_pdata * restrict const hw, const char * restrict const name)
{
	uint_fast8_t id;
	int ret = -ENOTFOUND;

	assert(hw && name);

	for (id = 0; (id < ARRAY_SIZE(hw->Sensors)); id++) {
		if (!hw->Sensors[id].set.configured)
			continue;
		if (!strcmp(hw->Sensors[id].name, name)) {
			ret = (int)id;
			break;
		}
	}

	return (ret);
}

/**
 * Find relay id by name.
 * @param hw private hw_p1 hardware data
 * @param name name to look for
 * @return -ENOTFOUND if not found, relay id if found
 */
int hw_p1_rid_by_name(const struct s_hw_p1_pdata * const restrict hw, const char * restrict const name)
{
	uint_fast8_t id;
	int ret = -ENOTFOUND;

	assert(hw && name);

	for (id = 0; (id < ARRAY_SIZE(hw->Relays)); id++) {
		if (!hw->Relays[id].set.configured)
			continue;
		if (!strcmp(hw->Relays[id].name, name)) {
			ret = (int)id;
			break;
		}
	}

	return (ret);
}
