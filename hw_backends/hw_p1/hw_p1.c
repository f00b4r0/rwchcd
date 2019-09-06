//
//  hw_backends/hw_p1/hw_p1.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 driver implementation.
 * @warning This driver can only accomodate a @b SINGLE instance of the hardware.
 * @note This driver should NOT be considered a good coding example, it is
 * heavily tailored to the context of a single prototype hardware controller
 * connected to a RaspberryPi GPIO header, and as such contains hardcoded values
 * and uses statically allocated data structures, which is deemed acceptable in
 * this particular context but should otherwise be frowned upon. The API in the
 * files hw_p1_backend.c, hw_p1_setup.c and hw_p1_filecfg.c is considered cleaner.
 * @note To build this driver, the `rwchc_export.h` header from the hardware's
 * firmware code is necessary.
 */

#include <time.h>	// time
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset/strdup
#include <assert.h>

#include "lib.h"
#include "storage.h"
#include "log.h"
#include "alarms.h"
#include "hw_lib.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1.h"

#define VALID_CALIB_MIN		(uint_fast16_t)(RWCHC_CALIB_OHM*0.9)	///< minimum valid calibration value (-10%)
#define VALID_CALIB_MAX		(uint_fast16_t)(RWCHC_CALIB_OHM*1.1)	///< maximum valid calibration value (+10%)

#define CALIBRATION_PERIOD	(600 * TIMEKEEP_SMULT)	///< calibration period in seconds: every 10mn

static const storage_version_t Hardware_sversion = 2;

/**
 * Log relays change.
 * @param hw HW P1 private data
 * @note This function isn't part of the timer system since it's more efficient
 * and more accurate to run it aperiodically (on relay edge).
 * @bug hardcoded identifier will collide if multiple instances.
 */
static void hw_p1_relays_log(const struct s_hw_p1_pdata * restrict const hw)
{
	const log_version_t version = 1;
	static const log_key_t keys[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "R1", "R2",
	};
	static log_value_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;
	
	assert(ARRAY_SIZE(keys) >= ARRAY_SIZE(hw->Relays));
	
	for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
		if (hw->Relays[i].set.configured) {
			if (hw->Relays[i].run.is_on)
				values[i] = 1;
			else
				values[i] = 0;
		}
		else
			values[i] = -1;
	}
	
	const struct s_log_data data = {
		.keys = keys,
		.values = values,
		.nkeys = ARRAY_SIZE(keys),
		.nvalues = i,
		.interval = -1,
	};
	log_async_dump("hw_p1_relays", &version, &data);
}

/**
 * Convert sensor value to actual resistance.
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
 * @param hw HW P1 private data
 * @param raw the raw sensor value
 * @param calib 1 if calibrated value is required, 0 otherwise
 * @return the resistance value
 */
__attribute__((pure)) static unsigned int sensor_to_ohm(const struct s_hw_p1_pdata * restrict const hw, const rwchc_sensor_t raw, const bool calib)
{
	static const uint_fast16_t dacset[] = RWCHC_DAC_STEPS;
	uint_fast16_t calibmult, dacoffset;
	uint_fast32_t value;

	dacoffset = (raw >> RWCHC_DAC_OFFBIT) & RWCHC_DAC_OFFMASK;

	value = raw & RWCHC_ADC_MAXV;		// raw is 10bit, cannot be negative when cast to sint
	value *= RWCHC_ADC_MVSCALE;		// convert to millivolts
	value += dacset[dacoffset]*RWCHC_DAC_MVSCALE*RWCHC_ADC_OPGAIN;	// add the initial offset

	/* value is now (1+RWCHC_ADC_OPGAIN) * actual value at sensor. Sensor is fed 0.5mA,
	 * so sensor resistance is RWCHC_ADC_RMULT * actual value in millivolt. */

	value *= RWCHC_ADC_RMULT;
	value /= (1+RWCHC_ADC_OPGAIN);

	// finally, apply calibration factor if any
	if (calib) {
		calibmult = dacoffset ? hw->run.calib_dac : hw->run.calib_nodac;
		value *= RWCHC_CALIB_OHM;
		value += calibmult/2;	// round
		value /= calibmult;
	}

	return (value);
}

/**
 * Raise an alarm for a specific sensor.
 * This function raises an alarm if the sensor's temperature is invalid.
 * @param hw HW P1 private data
 * @param id target sensor id
 * @param error sensor error
 * @return exec status
 */
static int sensor_alarm(const struct s_hw_p1_pdata * restrict const hw, const sid_t id, const int error)
{
	const char * restrict const msgf = _("sensor fail: \"%s\" (%d) %s");
	const char * restrict const msglcdf = _("sensor fail: %d");
	const char * restrict fail, * restrict name = NULL;
	char * restrict msg, * restrict msglcd;
	size_t size;
	int ret;

	switch (error) {
		case -ESENSORSHORT:
			fail = _("shorted");
			name = hw->Sensors[id-1].name;
			break;
		case -ESENSORDISCON:
			fail = _("disconnected");
			name = hw->Sensors[id-1].name;
			break;
		case -ESENSORINVAL:
			fail = _("invalid");
			break;
		default:
			fail = _("error");
			break;
	}

	snprintf_automalloc(msg, size, msgf, name, id, fail);
	snprintf_automalloc(msglcd, size, msglcdf, id);

	ret = alarms_raise(error, msg, msglcd);

	free(msg);
	free(msglcd);

	return (ret);
}

/**
 * Process raw sensor data.
 * Applies a short-window LP filter on raw data to smooth out noise.
 * Flag and raise alarm if value is out of #RWCHCD_TEMPMIN and #RWCHCD_TEMPMAX bounds.
 * @param hw HW P1 private data
 * @note the function implements a 5-sample delay on short/discon as well as a
 * 5-sample decimator on sudden changes of +/- 4C to work around a recent abnormal
 * behaviour on the revision 1.1 prototype hardware.
 */
static void hw_p1_parse_temps(struct s_hw_p1_pdata * restrict const hw)
{
	ohm_to_celsius_ft * o_to_c;
	uint_fast16_t ohm;
	uint_fast8_t i;
	temp_t previous, current;
	
	assert(hw->run.initialized);

	pthread_rwlock_wrlock(&hw->Sensors_rwlock);
	for (i = 0; i < hw->settings.nsensors; i++) {
		if (!hw->Sensors[i].set.configured) {
			hw->Sensors[i].run.value = TEMPUNSET;
			continue;
		}

		ohm = sensor_to_ohm(hw, hw->sensors[i], true);
		o_to_c = hw_lib_sensor_o_to_c(hw->Sensors[i].set.type);
		assert(o_to_c);

		current = celsius_to_temp(o_to_c(ohm)) + hw->Sensors[i].set.offset;
		previous = hw->Sensors[i].run.value;

		if (current <= RWCHCD_TEMPMIN) {
			// delay by hardcoded 5 samples
			if (hw->scount[i] < 5) {
				hw->scount[i]++;
				dbgmsg("delaying sensor %d short, samples ignored: %d", i+1, hw->scount[i]);
			}
			else {
				hw->Sensors[i].run.value = TEMPSHORT;
				sensor_alarm(hw, i+1, -ESENSORSHORT);
			}
		}
		else if (current >= RWCHCD_TEMPMAX) {
			// delay by hardcoded 5 samples
			if (hw->scount[i] < 5) {
				hw->scount[i]++;
				dbgmsg("delaying sensor %d disconnect, samples ignored: %d", i+1, hw->scount[i]);
			}
			else {
				hw->Sensors[i].run.value = TEMPDISCON;
				sensor_alarm(hw, i+1, -ESENSORDISCON);
			}
		}
		// init or recovery
		else if (previous <= TEMPINVALID) {
			hw->scount[i] = 0;
			hw->Sensors[i].run.value = current;
		}
		// normal operation
		else {
			// decimate large changes to work around measurement instability. Hardcoded 4C / 5 samples (i.e. ~5 seconds) max
			if (((current < (previous - deltaK_to_temp(4))) || (current > (previous + deltaK_to_temp(4)))) && hw->scount[i]++ < 5)
				dbgmsg("decimating sensor %d value, samples ignored: %d", i+1, hw->scount[i]);
			else {
				// apply LP filter - ensure we only apply filtering on valid temps
				hw->scount[i] = 0;
				hw->Sensors[i].run.value = temp_expw_mavg(previous, current, hw->set.nsamples, 1);
			}
		}
	}
	pthread_rwlock_unlock(&hw->Sensors_rwlock);
}

/**
 * Save hardware relays state to permanent storage.
 * @param hw HW P1 private data
 * @return exec status
 * @bug hardcoded identifier will collide if multiple instances.
 * @todo online save/restore from .run
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
 * hw_p1_setup_new()). When restoring relays we must:
 * - account for elapsed time in last known state (at save time)
 * - restore off_since to either the saved value or to the current time (if relay was last ON)
 */
int hw_p1_restore_relays(struct s_hw_p1_pdata * restrict const hw)
{
	static typeof (hw->Relays) blob;
	storage_version_t sversion;
	typeof(&hw->Relays[0]) relayptr = (typeof(relayptr))&blob;
	unsigned int i;
	int ret;
	
	// try to restore key elements of hardware
	ret = storage_fetch("hw_p1_relays", &sversion, blob, sizeof(blob));
	if (ALL_OK == ret) {
		if (Hardware_sversion != sversion)
			return (-EMISMATCH);

		for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
			// handle saved state (see @note)
			if (relayptr->run.is_on)
				hw->Relays[i].run.on_tottime += relayptr->run.state_time;
			else
				hw->Relays[i].run.off_tottime += relayptr->run.state_time;
			hw->Relays[i].run.off_since = timekeep_now();	// off since "now"
			hw->Relays[i].run.on_tottime += relayptr->run.on_tottime;
			hw->Relays[i].run.off_tottime += relayptr->run.off_tottime;
			hw->Relays[i].run.cycles += relayptr->run.cycles;
			relayptr++;
		}
		dbgmsg("Hardware relay state restored");
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

	// prepare hardware settings.deffail data
	hw_p1_rwchcsettings_deffail(hw);
	
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

	dbgmsg("HW Config saved.");
	
out:
	return (ret);
}

/**
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * This function uses a hardcoded moving average for all but the first calibration attempt,
 * to smooth out sudden bumps in calibration reads that could be due to noise.
 * @param hw HW P1 private data
 * @return exec status
 */
int hw_p1_calibrate(struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast16_t newcalib_nodac, newcalib_dac;
	int ret;
	rwchc_sensor_t ref;
	const timekeep_t now = timekeep_now();
	
	assert(hw->run.initialized);
	
	if (hw->run.last_calib && (now - hw->run.last_calib) < CALIBRATION_PERIOD)
		return (ALL_OK);

	ref = 0;
	ret = hw_p1_spi_ref_r(&hw->spi, &ref, 0);
	if (ret)
		return (ret);

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		newcalib_nodac = sensor_to_ohm(hw, ref, false);	// force uncalibrated read
		if ((newcalib_nodac < VALID_CALIB_MIN) || (newcalib_nodac > VALID_CALIB_MAX))	// don't store invalid values
			return (-EINVALID);	// should not happen
	}
	else
		return (-EINVALID);

	ref = 0;
	ret = hw_p1_spi_ref_r(&hw->spi, &ref, 1);
	if (ret)
		return (ret);

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		newcalib_dac = sensor_to_ohm(hw, ref, false);	// force uncalibrated read
		if ((newcalib_dac < VALID_CALIB_MIN) || (newcalib_dac > VALID_CALIB_MAX))	// don't store invalid values
			return (-EINVALID);	// should not happen
	}
	else
		return (-EINVALID);

	// everything went fine, we can update both calibration values and time
	hw->run.calib_nodac = hw->run.calib_nodac ? (uint_fast16_t)temp_expw_mavg(hw->run.calib_nodac, newcalib_nodac, 1, 5) : newcalib_nodac;	// hardcoded moving average (20% ponderation to new sample) to smooth out sudden bumps
	hw->run.calib_dac = hw->run.calib_dac ? (uint_fast16_t)temp_expw_mavg(hw->run.calib_dac, newcalib_dac, 1, 5) : newcalib_dac;		// hardcoded moving average (20% ponderation to new sample) to smooth out sudden bumps
	hw->run.last_calib = now;

	dbgmsg("NEW: calib_nodac: %d, calib_dac: %d", hw->run.calib_nodac, hw->run.calib_dac);
	
	return (ALL_OK);
}

/**
 * Read all temperature sensors.
 * This function will read all sensors (up to hw->settings.nsensors) into
 * hw->sensors and if no error occurs:
 * - hw->run.sensors_ftime will be updated
 * - Raw values from hw->sensors are processed to atomically update #hw->Sensors
 * otherwise these fields remain unchanged.
 *
 * @param hw HW P1 private data
 * @return exec status
 * @warning #hw->settings.nsensors must be set prior to calling this function.
 * @note calling hw_p1_parse_temps() in the success code path is a design choice that
 * ensures a consistent view of system temperatures:
 * either all values are updated coherently or none are.
 */
int hw_p1_sensors_read(struct s_hw_p1_pdata * restrict const hw)
{
	int_fast8_t sensor;
	int ret = ALL_OK;
	
	assert(hw->run.initialized);

	for (sensor = 0; sensor < hw->settings.nsensors; sensor++) {
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
 * Write all relays
 * This function updates all known hardware relays according to their desired turn_on
 * state. This function also does time and cycle accounting for the relays.
 * @note non-configured hardware relays are turned off.
 * @param hw HW P1 private data
 * @return status
 */
__attribute__((warn_unused_result)) int hw_p1_rwchcrelays_write(struct s_hw_p1_pdata * restrict const hw)
{
	struct s_hw_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const timekeep_t now = timekeep_now();	// we assume the whole thing will take much less than a second
	uint_fast8_t i, chflags = HW_LIB_RCHNONE;
	int ret = -EGENERIC;

	assert(hw->run.online);

	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
		relay = &hw->Relays[i];

		// perform relay accounting
		ret = hw_lib_relay_update(relay, now);
		if (ret < 0)
			continue;
		else
			chflags |= ret;

		// update internal structure
		rwchc_relay_set(&rWCHC_relays, i, relay->run.turn_on);
	}

	// save/log relays state if there was a change
	if (chflags) {
		hw_p1_relays_log(hw);
		if (chflags & HW_LIB_RCHTURNOFF) {	// only update permanent storage on full cycles (at turn off)
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
	unsigned int id;
	int ret = -ENOTFOUND;

	assert(hw && name);

	for (id = 0; (id < ARRAY_SIZE(hw->Sensors)); id++) {
		if (!hw->Sensors[id].set.configured)
			continue;
		if (!strcmp(hw->Sensors[id].name, name)) {
			ret = hw->Sensors[id].set.sid;
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
	unsigned int id;
	int ret = -ENOTFOUND;

	assert(hw && name);

	for (id = 0; (id < ARRAY_SIZE(hw->Relays)); id++) {
		if (!hw->Relays[id].set.configured)
			continue;
		if (!strcmp(hw->Relays[id].name, name)) {
			ret = hw->Relays[id].set.rid;
			break;
		}
	}

	return (ret);
}
