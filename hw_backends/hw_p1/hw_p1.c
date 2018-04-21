//
//  hw_p1.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 driver implementation.
 */

#include <time.h>	// time
#include <math.h>	// sqrtf
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset/strdup
#include <assert.h>

#include "lib.h"
#include "storage.h"
#include "alarms.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1.h"

#define VALID_CALIB_MIN		0.9F	///< minimum valid calibration value (-10%)
#define VALID_CALIB_MAX		1.1F	///< maximum valid calibration value (+10%)

#define CALIBRATION_PERIOD	600	///< calibration period in seconds: every 10mn

static const storage_version_t Hardware_sversion = 1;

struct s_hw_p1_pdata Hardware;	///< Prototype 1 private data

/**
 * Log relays change.
 * @note This function isn't part of the timer system since it's more efficient
 * and more accurate to run it aperiodically (on relay edge).
 */
static void hw_p1_relays_log(void)
{
	const storage_version_t version = 1;
	static storage_keys_t keys[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "R1", "R2",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;
	
	assert(ARRAY_SIZE(keys) >= ARRAY_SIZE(Hardware.Relays));
	
	for (i=0; i<ARRAY_SIZE(Hardware.Relays); i++) {
		if (Hardware.Relays[i].set.configured) {
			if (Hardware.Relays[i].run.is_on)
				values[i] = 1;
			else
				values[i] = 0;
		}
		else
			values[i] = -1;
	}
	
	storage_log("log_hw_p1_relays", &version, keys, values, i);
}

/**
 * Convert sensor value to actual resistance.
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
 * @param raw the raw sensor value
 * @param calib 1 if calibrated value is required, 0 otherwise
 * @return the resistance value
 */
__attribute__((pure)) static unsigned int sensor_to_ohm(const rwchc_sensor_t raw, const bool calib)
{
	const uint_fast16_t dacset[] = RWCHC_DAC_STEPS;
	uint_fast16_t value, dacoffset;
	float calibmult;

	dacoffset = (raw >> RWCHC_DAC_OFFBIT) & RWCHC_DAC_OFFMASK;

	value = raw & RWCHC_ADC_MAXV;		// raw is 10bit, cannot be negative when cast to sint
	value *= RWCHC_ADC_MVSCALE;		// convert to millivolts
	value += dacset[dacoffset]*RWCHC_DAC_MVSCALE*RWCHC_ADC_OPGAIN;	// add the initial offset

	/* value is now (1+RWCHC_ADC_OPGAIN) * actual value at sensor. Sensor is fed 0.5mA,
	 * so sensor resistance is RWCHC_ADC_RMULT * actual value in millivolt. */

	value *= RWCHC_ADC_RMULT;
	value /= (1+RWCHC_ADC_OPGAIN);

	// finally, apply calibration factor
	if (calib)
		calibmult = dacoffset ? Hardware.run.calib_dac : Hardware.run.calib_nodac;
	else
		calibmult = 1.0;

	value = ((float)value * calibmult);	// calibrate

	return (value);
}

// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

/**
 * Convert resistance value to actual temperature based on Callendar - Van Dusen.
 * Use a quadratic fit for simplicity.
 * http://aviatechno.net/thermo/rtd03.php
 * https://www.newport.com/medias/sys_master/images/images/h4b/h16/8797291446302/TN-RTD-1-Callendar-Van-Dusen-Equation-and-RTD-Temperature-Sensors.pdf
 * Rt = R0 + R0*alpha*[t - delta*(t/100 - 1)*(t/100) - beta*(t/100 - 1)*(t/100)^3]
 * alpha is the mean R change referred to 0C
 * Rt = R0 * [1 + A*t + B*t^2 - C*(t-100)*t^3]
 * A = alpha + (alpha*delta)/100
 * B = - (alpha * delta)/(100^2)
 * C = - (alpha * beta)/(100^4)
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
 * @return correct function pointer for sensor type or NULL if invalid type
 */
ohm_to_celsius_ft * hw_p1_sensor_o_to_c(const enum e_hw_p1_stype type)
{
	switch (type) {
		case ST_PT1000:
			return (pt1000_ohm_to_celsius);
		case ST_NI1000:
			return (ni1000_ohm_to_celsius);
		default:
			return (NULL);
	}
}

/**
 * Raise an alarm for a specific sensor.
 * This function raises an alarm if the sensor's temperature is invalid.
 * @param id target sensor id
 * @param error sensor error
 * @return exec status
 */
static int sensor_alarm(const sid_t id, const int error)
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
			name = Hardware.Sensors[id-1].name;
			break;
		case -ESENSORDISCON:
			fail = _("disconnected");
			name = Hardware.Sensors[id-1].name;
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
 */
void parse_temps(void)
{
	ohm_to_celsius_ft * o_to_c;
	uint_fast16_t ohm;
	uint_fast8_t i;
	temp_t previous, current;
	
	assert(Hardware.run.online);

	pthread_rwlock_wrlock(&Hardware.Sensors_rwlock);
	for (i = 0; i < Hardware.settings.nsensors; i++) {
		if (!Hardware.Sensors[i].set.configured) {
			Hardware.Sensors[i].run.value = TEMPUNSET;
			continue;
		}

		ohm = sensor_to_ohm(Hardware.sensors[i], true);
		o_to_c = Hardware.Sensors[i].ohm_to_celsius;
		assert(o_to_c);

		current = celsius_to_temp(o_to_c(ohm)) + Hardware.Sensors[i].set.offset;
		previous = Hardware.Sensors[i].run.value;

		if (current <= RWCHCD_TEMPMIN) {
			Hardware.Sensors[i].run.value = TEMPSHORT;
			sensor_alarm(i+1, -ESENSORSHORT);
		}
		else if (current >= RWCHCD_TEMPMAX) {
			Hardware.Sensors[i].run.value = TEMPDISCON;
			sensor_alarm(i+1, -ESENSORDISCON);
		}
		else
			// apply LP filter - ensure we only apply filtering on valid temps
			Hardware.Sensors[i].run.value = (previous > TEMPINVALID) ? temp_expw_mavg(previous, current, Hardware.set.nsamples, 1) : current;
	}
	pthread_rwlock_unlock(&Hardware.Sensors_rwlock);

}

/**
 * Save hardware relays state to permanent storage
 * @return exec status
 * @todo online save/restore from .run
 */
int hw_p1_save_relays(void)
{
	return (storage_dump("hw_p1_relays", &Hardware_sversion, Hardware.Relays, sizeof(Hardware.Relays)));
}

/**
 * Restore hardware relays state from permanent storage
 * Restores cycles and on/off total time counts for all relays.
 * @return exec status
 * @todo restore relay name
 */
int hw_p1_restore_relays(void)
{
	static typeof (Hardware.Relays) blob;
	storage_version_t sversion;
	typeof(&Hardware.Relays[0]) relayptr = (typeof(relayptr))&blob;
	unsigned int i;
	int ret;
	
	// try to restore key elements of hardware
	ret = storage_fetch("hw_p1_relays", &sversion, blob, sizeof(blob));
	if (ALL_OK == ret) {
		if (Hardware_sversion != sversion)
			return (-EMISMATCH);

		for (i=0; i<ARRAY_SIZE(Hardware.Relays); i++) {
			if (relayptr->run.is_on)	// account for last known state_time
				Hardware.Relays[i].run.on_tottime += relayptr->run.state_time;
			else
				Hardware.Relays[i].run.off_tottime += relayptr->run.state_time;
			Hardware.Relays[i].run.on_tottime += relayptr->run.on_tottime;
			Hardware.Relays[i].run.off_tottime += relayptr->run.off_tottime;
			Hardware.Relays[i].run.cycles += relayptr->run.cycles;
			relayptr++;
		}
		dbgmsg("Hardware relay state restored");
	}

	return (ret);
}

/**
 * Log internal temperatures.
 * @return exec status
 * @warning Locks runtime: do not call from master_thread
 */
int hw_p1_async_log_temps(void)
{
	const storage_version_t version = 2;
	static storage_keys_t keys[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	int i = 0;

	assert(ARRAY_SIZE(keys) >= RWCHC_NTSENSORS);

	pthread_rwlock_rdlock(&Hardware.Sensors_rwlock);
	for (i = 0; i < Hardware.settings.nsensors; i++)
		values[i] = Hardware.Sensors[i].run.value;
	pthread_rwlock_unlock(&Hardware.Sensors_rwlock);

	return (storage_log("log_hw_p1_temps", &version, keys, values, i));
}

/**
 * Read hardware config.
 * @param settings target hardware configuration
 * @return exec status
 */
static int hw_p1_hwconfig_fetch(struct rwchc_s_settings * const settings)
{
	return (hw_p1_spi_settings_r(settings));
}

/**
 * Commit hardware config to hardware.
 * @note overwrites all hardware settings.
 * @return exec status
 */
int hw_p1_hwconfig_commit(void)
{
	struct rwchc_s_settings hw_set;
	int ret;
	
	if (!Hardware.run.initialized)
		return (-EOFFLINE);
	
	// grab current config from the hardware
	ret = hw_p1_hwconfig_fetch(&hw_set);
	if (ret)
		goto out;
	
	if (!memcmp(&hw_set, &(Hardware.settings), sizeof(hw_set)))
		return (ALL_OK); // don't wear flash down if unnecessary
	
	// commit hardware config
	ret = hw_p1_spi_settings_w(&(Hardware.settings));
	if (ret)
		goto out;

	// check that the data is correct on target
	ret = hw_p1_hwconfig_fetch(&hw_set);
	if (ret)
		goto out;

	if (memcmp(&hw_set, &(Hardware.settings), sizeof(hw_set))) {
		ret = -EHARDWARE;
		goto out;
	}
	
	// save hardware config
	ret = hw_p1_spi_settings_s();

	dbgmsg("HW Config saved.");
	
out:
	return (ret);
}

/**
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * This function uses a hardcoded moving average for all but the first calibration attempt,
 * to smooth out sudden bumps in calibration reads that could be due to noise.
 * @return error status
 */
int hw_p1_calibrate(void)
{
	float newcalib_nodac, newcalib_dac;
	uint_fast16_t refcalib;
	int ret;
	rwchc_sensor_t ref;
	const time_t now = time(NULL);
	
	assert(Hardware.run.initialized);
	
	if ((now - Hardware.run.last_calib) < CALIBRATION_PERIOD)
		return (ALL_OK);

	dbgmsg("OLD: calib_nodac: %f, calib_dac: %f", Hardware.run.calib_nodac, Hardware.run.calib_dac);
	
	ret = hw_p1_spi_ref_r(&ref, 0);
	if (ret)
		return (ret);

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, false);	// force uncalibrated read
		newcalib_nodac = ((float)RWCHC_CALIB_OHM / (float)refcalib);
		if ((newcalib_nodac < VALID_CALIB_MIN) || (newcalib_nodac > VALID_CALIB_MAX))	// don't store invalid values
			return (-EINVALID);	// should not happen
	}
	else
		return (-EINVALID);

	ret = hw_p1_spi_ref_r(&ref, 1);
	if (ret)
		return (ret);

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, false);	// force uncalibrated read
		newcalib_dac = ((float)RWCHC_CALIB_OHM / (float)refcalib);
		if ((newcalib_dac < VALID_CALIB_MIN) || (newcalib_dac > VALID_CALIB_MAX))	// don't store invalid values
			return (-EINVALID);	// should not happen
	}
	else
		return (-EINVALID);

	// everything went fine, we can update both calibration values and time
	Hardware.run.calib_nodac = Hardware.run.calib_nodac ? (Hardware.run.calib_nodac - (0.20F * (Hardware.run.calib_nodac - newcalib_nodac))) : newcalib_nodac;	// hardcoded moving average (20% ponderation to new sample) to smooth out sudden bumps
	Hardware.run.calib_dac = Hardware.run.calib_dac ? (Hardware.run.calib_dac - (0.20F * (Hardware.run.calib_dac - newcalib_dac))) : newcalib_dac;		// hardcoded moving average (20% ponderation to new sample) to smooth out sudden bumps
	Hardware.run.last_calib = now;

	dbgmsg("NEW: calib_nodac: %f, calib_dac: %f", Hardware.run.calib_nodac, Hardware.run.calib_dac);
	
	return (ALL_OK);
}

/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @return exec status
 * @warning Hardware.settings.nsensors must be set prior to calling this function
 */
int hw_p1_sensors_read(rwchc_sensor_t tsensors[])
{
	int_fast8_t sensor;
	int ret = ALL_OK;
	
	assert(Hardware.run.online);

	for (sensor = 0; sensor < Hardware.settings.nsensors; sensor++) {
		ret = hw_p1_spi_sensor_r(tsensors, sensor);
		if (ret)
			goto out;
	}

out:
	return (ret);
}

/**
 * Update internal relay system based on target state.
 * @param rWCHC_relays target internal relay system
 * @param id target relay id (from 0)
 * @param state target state
 */
__attribute__((always_inline)) inline void rwchc_relay_set(union rwchc_u_relays * const rWCHC_relays, const rid_t id, const bool state)
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
 * Write all relays
 * This function updates all known hardware relays according to their desired turn_on
 * state. This function also does time and cycle accounting for the relays.
 * @note non-configured hardware relays are turned off.
 * @return status
 */
__attribute__((warn_unused_result)) int hw_p1_rwchcrelays_write(void)
{
#define CHNONE		0x00
#define CHTURNON	0x01
#define CHTURNOFF	0x02
	struct s_stateful_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const time_t now = time(NULL);	// we assume the whole thing will take much less than a second
	uint_fast8_t i, chflags = CHNONE;
	int ret = -EGENERIC;

	assert(Hardware.run.online);

	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<ARRAY_SIZE(Hardware.Relays); i++) {
		relay = &Hardware.Relays[i];

		if (!relay->set.configured)
			continue;

		// update state counters at state change
		if (relay->run.turn_on) {	// turn on
			if (!relay->run.is_on) {	// relay is currently off
				relay->run.cycles++;	// increment cycle count
				relay->run.is_on = true;
				relay->run.on_since = now;
				if (relay->run.off_since)
					relay->run.off_tottime += now - relay->run.off_since;
				relay->run.off_since = 0;
				chflags |= CHTURNON;
			}
		}
		else {	// turn off
			if (relay->run.is_on) {	// relay is currently on
				relay->run.is_on = false;
				relay->run.off_since = now;
				if (relay->run.on_since)
					relay->run.on_tottime += now - relay->run.on_since;
				relay->run.on_since = 0;
				chflags |= CHTURNOFF;
			}
		}

		// update state time counter
		relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

		// update internal structure
		rwchc_relay_set(&rWCHC_relays, i, relay->run.turn_on);
	}

	// save/log relays state if there was a change
	if (chflags) {
		hw_p1_relays_log();
		if (chflags & CHTURNOFF) {	// only update permanent storage on full cycles (at turn off)
			// XXX there's no real motive to do this besides lowering storage pressure
			ret = hw_p1_save_relays();
			if (ret)
				dbgerr("hw_p1_save failed (%d)", ret);
		}
	}
	
	// send new state to hardware
	ret = hw_p1_spi_relays_w(&rWCHC_relays);

	// update internal runtime state on success
	if (ALL_OK == ret)
		Hardware.relays.ALL = rWCHC_relays.ALL;

	return (ret);
}

/**
 * Write all peripherals from internal runtime to hardware
 * @return status
 */
__attribute__((warn_unused_result)) inline int hw_p1_rwchcperiphs_write(void)
{
	assert(Hardware.run.online);
	return (hw_p1_spi_peripherals_w(&(Hardware.peripherals)));
}

/**
 * Read all peripherals from hardware into internal runtime
 * @return exec status
 */
__attribute__((warn_unused_result)) inline int hw_p1_rwchcperiphs_read(void)
{
	assert(Hardware.run.online);
	return (hw_p1_spi_peripherals_r(&(Hardware.peripherals)));
}

int hw_p1_sensor_clone_temp(void * priv, const sid_t id, temp_t * const tclone);
//* XXX quick hack for LCD
const char * hw_p1_temp_to_str(const sid_t tempid)
{
	static char snpbuf[10];	// xXX.XC, null-terminated (first x negative sign or positive hundreds)
	temp_t temp;
	float celsius;
	int ret;

	ret = hw_p1_sensor_clone_temp(&Hardware, tempid, &temp);

#if (RWCHCD_TEMPMIN < ((-99 + 273) * KPRECISIONI))
#error Non representable minimum temperature
#endif

	snprintf(snpbuf, 4, "%2d:", tempid);	// print in human readable

	if (-ESENSORDISCON == ret)
		strncpy(snpbuf+3, _("DISCON"), 6);
	else if (-ESENSORSHORT == ret)
		strncpy(snpbuf+3, _("SHORT "), 6);	// must be 6 otherwith buf[6] might be garbage
	else {
		celsius = temp_to_celsius(temp);
		snprintf(snpbuf+3, 7, "%5.1fC", celsius);	// handles rounding
	}

	return (&snpbuf);
}

/**
 * Find sensor id by name.
 * @param name name to look for
 * @return -ENOTFOUND if not found, sensor id if found
 */
int hw_p1_sid_by_name(const char * const name)
{
	unsigned int id;
	int ret = -ENOTFOUND;

	for (id = 0; (id < ARRAY_SIZE(Hardware.Sensors)); id++) {
		if (!Hardware.Sensors[id].set.configured)
			continue;
		if (!strcmp(Hardware.Sensors[id].name, name)) {
			ret = id+1;
			break;
		}
	}

	return (ret);
}

/**
 * Find relay id by name.
 * @param name name to look for
 * @return -ENOTFOUND if not found, sensor id if found
 */
int hw_p1_rid_by_name(const char * const name)
{
	unsigned int id;
	int ret = -ENOTFOUND;

	for (id = 0; (id < ARRAY_SIZE(Hardware.Relays)); id++) {
		if (!Hardware.Relays[id].set.configured)
			continue;
		if (!strcmp(Hardware.Relays[id].name, name)) {
			ret = id+1;
			break;
		}
	}

	return (ret);
}