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
#include <stdio.h>	// asprintf

#include "lib.h"
#include "storage.h"
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

	return (value);
}

/**
 * Process raw sensor data.
 * Flag and raise alarm if value is out of #RWCHCD_TEMPMIN and #RWCHCD_TEMPMAX bounds.
 * @param hw HW P1 private data
 */
static void hw_p1_parse_temps(struct s_hw_p1_pdata * restrict const hw)
{
	struct s_hw_p1_sensor * sensor;
	uint_fast8_t i, refid;
	temp_t current;
	res_t res;

	assert(hw->run.initialized);

	for (i = 0; i < hw->run.nsensors; i++) {
		sensor = &hw->Sensors[i];
		if (!sensor->set.configured) {
			aser(&sensor->run.value, TEMPUNSET);
			continue;
		}

		res = sensor_to_res(hw->sensors[sensor->set.channel-1]);
		refid = sensor->set.channel <= 7 ? 0 : 1;
		res /= hw->refs[refid] * 4U;

		// R0 hardcoded: HWP1 only supports R0 = 1000 ohms
		current = celsius_to_temp(hw_lib_rtd_res_to_celsius(sensor->set.type, hw_lib_ohm_to_res(1000), res));

		dbgmsg(2, 1, "s%d, raw: %x, res: %d, temp: %.2f", sensor->set.channel, hw->sensors[sensor->set.channel-1], res, temp_to_celsius(current));

		if (unlikely(current <= RWCHCD_TEMPMIN)) {
			aser(&sensor->run.value, TEMPSHORT);
			dbgerr("\"%s\": sensor \"%s\" (%d): shorted", hw->name, sensor->name, sensor->set.channel);
		}
		else if (unlikely(current >= RWCHCD_TEMPMAX)) {
			aser(&sensor->run.value, TEMPDISCON);
			dbgerr("\"%s\": sensor \"%s\" (%d): disconnected", hw->name, sensor->name, sensor->set.channel);
		}
		else
			aser(&sensor->run.value, current);
	}
}

/**
 * Save hardware relays state to permanent storage.
 * @param hw HW P1 private data
 * @return exec status
 */
int hw_p1_save_relays(const struct s_hw_p1_pdata * restrict const hw)
{
	char * storename;
	int ret;

	assert(hw->run.online);

	ret = asprintf(&storename, "hwp1_%s_relays", hw->name);
	if (ret < 0)
		return (-EOOM);

	ret = storage_dump(storename, &Hardware_sversion, hw->Relays, sizeof(hw->Relays));
	free(storename);
	return (ret);
}

/**
 * Restore hardware relays state from permanent storage.
 * Restores cycles and on/off total time counts for all relays.
 * @param hw HW P1 private data
 * @return exec status
 * @note Each relay is "restored" in OFF state (due to initialization in
 * hw_p1_setup_new()).
 */
int hw_p1_restore_relays(struct s_hw_p1_pdata * restrict const hw)
{
	const timekeep_t now = timekeep_now();
	static typeof (hw->Relays) blob;
	storage_version_t sversion;
	typeof(&hw->Relays[0]) blobptr = (typeof(blobptr))&blob;
	char * storename;
	unsigned int i;
	int ret;

	ret = asprintf(&storename, "hwp1_%s_relays", hw->name);
	if (ret < 0)
		return (-EOOM);

	// try to restore key elements of hardware
	ret = storage_fetch(storename, &sversion, blob, sizeof(blob));
	free(storename);
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
		dbgmsg(1, 1, "\"%s\" Hardware relay state restored", hw->name);
	}

	return (ret);
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
		switch (hw->Relays[i].set.channel) {
			case 1:
				hw->settings.deffail.RL1 = hw->Relays[i].set.failstate;
				break;
			case 2:
				hw->settings.deffail.RL2 = hw->Relays[i].set.failstate;
				break;
			case 3:
				hw->settings.deffail.T1 = hw->Relays[i].set.failstate;
				break;
			case 4:
				hw->settings.deffail.T2 = hw->Relays[i].set.failstate;
				break;
			case 5:
				hw->settings.deffail.T3 = hw->Relays[i].set.failstate;
				break;
			case 6:
				hw->settings.deffail.T4 = hw->Relays[i].set.failstate;
				break;
			case 7:
				hw->settings.deffail.T5 = hw->Relays[i].set.failstate;
				break;
			case 8:
				hw->settings.deffail.T6 = hw->Relays[i].set.failstate;
				break;
			case 9:
				hw->settings.deffail.T7 = hw->Relays[i].set.failstate;
				break;
			case 10:
				hw->settings.deffail.T8 = hw->Relays[i].set.failstate;
				break;
			case 11:
				hw->settings.deffail.T9 = hw->Relays[i].set.failstate;
				break;
			case 12:
				hw->settings.deffail.T10 = hw->Relays[i].set.failstate;
				break;
			case 13:
				hw->settings.deffail.T11 = hw->Relays[i].set.failstate;
				break;
			case 14:
				hw->settings.deffail.T12 = hw->Relays[i].set.failstate;
				break;
			default:
				dbgerr("\"%s\" Invalid relay channel: %d", hw->name, hw->Sensors[i].set.channel);
				break;
		}
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
				dbgerr("\"%s\" Invalid sensor channel: %d", hw->name, hw->Sensors[i].set.channel);
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

	dbgmsg(1, 1, "\"%s\" HW Config saved.", hw->name);
	
out:
	return (ret);
}

/**
 * Read all temperature calibration references.
 * @param hw HW P1 private data
 * @return exec status
 */
int hw_p1_refs_read(struct s_hw_p1_pdata * restrict const hw)
{
	int ret = ALL_OK;

	assert(hw->run.initialized);

	ret = hw_p1_spi_refs_r(&hw->spi, hw->refs);

	dbgmsg(2, 1, "\"%s\" ref0 raw: %x", hw->name, hw->refs[0]);
	dbgmsg(2, 1, "\"%s\" ref1 raw: %x", hw->name, hw->refs[1]);

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
	int ret = ALL_OK;
	
	assert(hw->run.initialized);

	ret = hw_p1_spi_sensors_r(&hw->spi, hw->sensors);
	if (ret)
		goto out;

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
 * Update internal relay system based on target state.
 * This function takes a physical relay channel and adjusts the internal
 * hardware data structure based on the desired relay state.
 * @param rWCHC_relays target internal relay system
 * @param channel target relay channel (from 1)
 * @param state target state
 */
__attribute__((always_inline)) static inline void rwchc_relay_set(union rwchc_u_relays * const rWCHC_relays, const uint_fast8_t channel, const bool state)
{
	uint_fast8_t rid = channel;

	rid--;	// indexing starts at 0
	
	// adapt relay id - XXX WARNING this assumes relays are consecutive in the structure with a hole at bit 7.
	if (rid > 6)
		rid++;	// skip the hole

	// set state
	if (state)
		setbit(rWCHC_relays->ALL, rid);
	else
		clrbit(rWCHC_relays->ALL, rid);
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
		rwchc_relay_set(&rWCHC_relays, relay->set.channel, (bool)relay->run.is_on);
	}

	// save/log relays state if there was a change
	if (chflags) {
		ret = hw_p1_save_relays(hw);
		if (ret)
			dbgerr("\"%s\" hw_p1_save failed (%d)", hw->name, ret);
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
