//
//  hardware.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware interface implementation.
 */

#include <time.h>	// time
#include <math.h>	// sqrtf
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset/strdup
#include <unistd.h>	// sleep
#include <assert.h>

#include "rwchcd.h"
#include "spi.h"
#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "storage.h"
#include "lcd.h"
#include "alarms.h"
#include "hardware.h"

#include "rwchc_export.h"

#if RWCHC_NTSENSORS != RWCHCD_NTEMPS
 #error Discrepancy in number of hardware sensors
#endif

#define RWCHCD_INIT_MAX_TRIES	10	///< how many times hardware init should be retried

#define RELAY_MAX_ID		14	///< maximum valid relay id

#define VALID_CALIB_MIN		0.9F	///< minimum valid calibration value (-10%)
#define VALID_CALIB_MAX		1.1F	///< maximum valid calibration value (+10%)

#define CALIBRATION_PERIOD	600	///< calibration period in seconds: every 10mn

/** software representation of a hardware relay */
struct s_stateful_relay {
	struct {
		bool configured;	///< true if properly configured
		uint_fast8_t id;	///< NOT USED
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
	char * restrict name;		///< user-defined name for the relay
};

static const storage_version_t Hardware_sversion = 1;

static struct s_stateful_relay Relays[RELAY_MAX_ID];	///< physical relays

typedef float ohm_to_celsius_ft(const uint_fast16_t);	///< ohm-to-celsius function prototype

/** software representation of a temperature sensor */
struct s_sensor {
	struct {
		bool configured;
		enum e_sensor_type type;
	} set;
	struct {
		temp_t value;
	} run;
	ohm_to_celsius_ft * ohm_to_celsius;
	char * restrict name;
};

static struct s_sensor Sensors[RWCHCD_NTEMPS];		///< physical sensors

static struct {
	bool ready;			///< hardware is ready
	time_t sensors_ftime;		///< sensors fetch time
	time_t last_calib;		///< time of last calibration
	float calib_nodac;		///< sensor calibration value without dac offset
	float calib_dac;		///< sensor calibration value with dac offset
	int fwversion;			///< firmware version
	struct rwchc_s_settings settings;
	union rwchc_u_relays relays;
	union rwchc_u_periphs peripherals;
	rwchc_sensor_t sensors[RWCHC_NTSENSORS];
} Hardware;

/**
 * Log relays change.
 * @note This function isn't part of the timer system since it's more efficient
 * and more accurate to run it aperiodically (on relay edge).
 */
static void hardware_relays_log(void)
{
	const storage_version_t version = 1;
	static storage_keys_t keys[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "R1", "R2",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;
	
	assert(ARRAY_SIZE(keys) >= ARRAY_SIZE(Relays));
	
	for (i=0; i<ARRAY_SIZE(Relays); i++) {
		if (Relays[i].set.configured) {
			if (Relays[i].run.is_on)
				values[i] = 1;
			else
				values[i] = 0;
		}
		else
			values[i] = -1;
	}
	
	storage_log("log_hw_relays", &version, keys, values, i);
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
		calibmult = dacoffset ? Hardware.calib_dac : Hardware.calib_nodac;
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
static ohm_to_celsius_ft * sensor_o_to_c(const enum e_sensor_type type)
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
 * @param temp sensor temperature
 * @return exec status
 */
static int sensor_alarm(const tempid_t id, const temp_t temp)
{
	const char * restrict const msgf = _("sensor fail: %s (%d) %s");
	const char * restrict const msglcdf = _("sensor fail: %d");
	const char * restrict fail, * restrict name = NULL;
	char * restrict msg, * restrict msglcd;
	size_t size;
	int ret;

	ret = validate_temp(temp);

	switch (ret) {
		case -ESENSORSHORT:
			fail = _("shorted");
			name = Sensors[id-1].name;
			break;
		case -ESENSORDISCON:
			fail = _("disconnected");
			name = Sensors[id-1].name;
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

	ret = alarms_raise(ret, msg, msglcd);

	free(msg);
	free(msglcd);

	return (ret);
}

/**
 * Process raw sensor data and extract temperature values into the runtime temps[] array.
 * Applies a short-window LP filter on raw data to smooth out noise.
 */
static void parse_temps(void)
{
	struct s_runtime * const runtime = get_runtime();
	const uint_fast8_t nsamples = runtime->config->temp_nsamples;
	ohm_to_celsius_ft * o_to_c;
	uint_fast16_t ohm;
	uint_fast8_t i;
	temp_t previous, current;
	
	assert(Hardware.ready && runtime);
	
	for (i = 0; i < runtime->config->nsensors; i++) {
		if (!Sensors[i].set.configured) {
			Sensors[i].run.value = TEMPUNSET;
			continue;
		}

		ohm = sensor_to_ohm(Hardware.sensors[i], true);
		o_to_c = Sensors[i].ohm_to_celsius;
		assert(o_to_c);

		current = celsius_to_temp(o_to_c(ohm));
		previous = Sensors[i].run.value;

		if (current <= RWCHCD_TEMPMIN) {
			Sensors[i].run.value = TEMPSHORT;
			sensor_alarm(i+1, TEMPSHORT);
		}
		else if (current >= RWCHCD_TEMPMAX) {
			Sensors[i].run.value = TEMPDISCON;
			sensor_alarm(i+1, TEMPDISCON);
		}
		else
			// apply LP filter - ensure we only apply filtering on valid temps
			Sensors[i].run.value = (previous >= TEMPINVALID) ? temp_expw_mavg(previous, current, nsamples, 1) : current;
	}

	pthread_rwlock_wrlock(&runtime->runtime_rwlock);
	for (i = 0; i < runtime->config->nsensors; i++)
		runtime->temps[i] = Sensors[i].run.value;
	runtime->temps_time = Hardware.sensors_ftime;
	pthread_rwlock_unlock(&runtime->runtime_rwlock);
}

/**
 * Save hardware relays state to permanent storage
 * @return exec status
 * @todo proper save of relay name
 */
static int hardware_save_relays(void)
{
	return (storage_dump("hardware_relays", &Hardware_sversion, Relays, sizeof(Relays)));
}

/**
 * Restore hardware relays state from permanent storage
 * Restores cycles and on/off total time counts for all relays.
 * @return exec status
 * @todo restore relay name
 */
static int hardware_restore_relays(void)
{
	static typeof (Relays) blob;
	storage_version_t sversion;
	typeof(&Relays[0]) relayptr = (typeof(relayptr))&blob;
	unsigned int i;
	int ret;
	
	// try to restore key elements of hardware
	ret = storage_fetch("hardware_relays", &sversion, blob, sizeof(blob));
	if (ALL_OK == ret) {
		if (Hardware_sversion != sversion)
			return (-EMISMATCH);

		for (i=0; i<ARRAY_SIZE(Relays); i++) {
			if (relayptr->run.is_on)	// account for last known state_time
				Relays[i].run.on_tottime += relayptr->run.state_time;
			else
				Relays[i].run.off_tottime += relayptr->run.state_time;
			Relays[i].run.on_tottime += relayptr->run.on_tottime;
			Relays[i].run.off_tottime += relayptr->run.off_tottime;
			Relays[i].run.cycles += relayptr->run.cycles;
			relayptr++;
		}
	}
	else
		dbgmsg("storage_fetch failed");

	return (ret);
}

/**
 * Save hardware sensors to permanent storage
 * @return exec status
 * @todo proper save of sensor name
 */
static int hardware_save_sensors(void)
{
	return (storage_dump("hardware_sensors", &Hardware_sversion, Sensors, sizeof(Sensors)));
}

/**
 * Restore hardware sensor config from permanent storage
 * Restores converter callback for set sensors.
 * @return exec status
 * @todo restore sensor name
 */
static int hardware_restore_sensors(void)
{
	static typeof (Sensors) blob;
	storage_version_t sversion;
	typeof(&Sensors[0]) sensorptr = (typeof(sensorptr))&blob;
	unsigned int i;
	int ret;

	// try to restore key elements of hardware
	ret = storage_fetch("hardware_sensors", &sversion, blob, sizeof(blob));
	if (ALL_OK == ret) {
		if (Hardware_sversion != sversion)
			return (-EMISMATCH);

		for (i=0; i<ARRAY_SIZE(Sensors); i++) {
			if (!sensorptr->set.configured)
				continue;

			Sensors[i].set.type = sensorptr->set.type;
			Sensors[i].ohm_to_celsius = sensor_o_to_c(sensorptr->set.type);
			if (Sensors[i].ohm_to_celsius)
				Sensors[i].set.configured = true;
		}
	}
	else
		dbgmsg("storage_fetch failed");

	return (ret);
}

/**
 * Set hardware configuration for LCD backlight level.
 * @param percent backlight level (0 = off, 100 = full)
 * @return exec status
 */
int hardware_config_setbl(const uint8_t percent)
{
	if (!Hardware.ready)
		return (-EOFFLINE);

	if (percent > 100)
		return (-EINVALID);

	Hardware.settings.lcdblpct = percent;

	return (ALL_OK);
}

/**
 * Set hardware configuration for number of sensors.
 * @param lastid last connected sensor id
 * @return exec status
 */
int hardware_config_setnsensors(const relid_t lastid)
{
	if (!Hardware.ready)
		return (-EOFFLINE);

	if ((lastid <= 0) || (lastid > RWCHC_NTSENSORS))
		return (-EINVALID);

	Hardware.settings.nsensors = lastid;

	return (ALL_OK);
}

/**
 * Read hardware config.
 * @param settings target hardware configuration
 * @return exec status
 */
static int hardware_config_fetch(struct rwchc_s_settings * const settings)
{
	return (spi_settings_r(settings));
}

/**
 * Commit and save hardware config.
 * @return exec status
 */
int hardware_config_store(void)
{
	struct rwchc_s_settings hw_set;
	int ret;
	
	if (!Hardware.ready)
		return (-EOFFLINE);
	
	// grab current config from the hardware
	hardware_config_fetch(&hw_set);
	
	if (!memcmp(&hw_set, &(Hardware.settings), sizeof(hw_set)))
		return (ALL_OK); // don't wear flash down if unnecessary
	
	// commit hardware config
	ret = spi_settings_w(&(Hardware.settings));
	if (ret)
		goto out;
	
	// save hardware config
	ret = spi_settings_s();

	dbgmsg("HW Config saved.");
	
out:
	return (ret);
}

/**
 * Initialize hardware and ensure connection is set
 * @return error state
 */
int hardware_init(void)
{
	int ret, i = 0;
	
	if (spi_init() < 0)
		return (-EINIT);

	memset(Relays, 0x0, sizeof(Relays));
	memset(Sensors, 0x0, sizeof(Sensors));
	memset(&Hardware, 0x0, sizeof(Hardware));

	// fetch firmware version
	do {
		ret = spi_fwversion();
	} while ((ret <= 0) && (i++ < RWCHCD_INIT_MAX_TRIES));

	if (ret > 0) {
		pr_log("Firmware version %d detected", ret);
		Hardware.fwversion = ret;
		// fetch hardware config
		ret = hardware_config_fetch(&(Hardware.settings));
		Hardware.ready = true;
	}
	else
		dbgerr("hardware_init failed");

	return (ret);
}

/**
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * This function uses a hardcoded moving average for all but the first calibration attempt,
 * to smooth out sudden bumps in calibration reads that could be due to noise.
 * @return error status
 */
static int hardware_calibrate(void)
{
	float newcalib_nodac, newcalib_dac;
	uint_fast16_t refcalib;
	int ret;
	rwchc_sensor_t ref;
	const time_t now = time(NULL);
	
	assert(Hardware.ready);
	
	if ((now - Hardware.last_calib) < CALIBRATION_PERIOD)
		return (ALL_OK);

	dbgmsg("OLD: calib_nodac: %f, calib_dac: %f", Hardware.calib_nodac, Hardware.calib_dac);
	
	ret = spi_ref_r(&ref, 0);
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

	ret = spi_ref_r(&ref, 1);
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
	Hardware.calib_nodac = Hardware.calib_nodac ? (Hardware.calib_nodac - (0.10F * (Hardware.calib_nodac - newcalib_nodac))) : newcalib_nodac;	// hardcoded moving average (10% ponderation to new sample) to smooth out sudden bumps
	Hardware.calib_dac = Hardware.calib_dac ? (Hardware.calib_dac - (0.10F * (Hardware.calib_dac - newcalib_dac))) : newcalib_dac;		// hardcoded moving average (10% ponderation to new sample) to smooth out sudden bumps
	Hardware.last_calib = now;

	dbgmsg("NEW: calib_nodac: %f, calib_dac: %f", Hardware.calib_nodac, Hardware.calib_dac);
	
	return (ALL_OK);
}

/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @param last the id of the last wanted (connected) sensor
 * @return exec status
 * @warning Hardware.settings.addresses.nsensors must be set prior to calling this function
 */
static int hardware_sensors_read(rwchc_sensor_t tsensors[])
{
	int_fast8_t sensor;
	int ret = ALL_OK;
	
	assert(Hardware.ready);

	for (sensor = 0; sensor < Hardware.settings.nsensors; sensor++) {
		ret = spi_sensor_r(tsensors, sensor);
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
__attribute__((always_inline)) static inline void rwchc_relay_set(union rwchc_u_relays * const rWCHC_relays, const relid_t id, const bool state)
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
__attribute__((warn_unused_result)) static int hardware_rwchcrelays_write(void)
{
	struct s_stateful_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const time_t now = time(NULL);	// we assume the whole thing will take much less than a second
	uint_fast8_t i;
	enum {CHNONE = 0, CHTURNON, CHTURNOFF } change = CHNONE;
	int ret = -EGENERIC;

	if (!Hardware.ready)
		return (-EOFFLINE);
	
	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<ARRAY_SIZE(Relays); i++) {
		relay = &Relays[i];

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
				change = CHTURNON;
			}
		}
		else {	// turn off
			if (relay->run.is_on) {	// relay is currently on
				relay->run.is_on = false;
				relay->run.off_since = now;
				if (relay->run.on_since)
					relay->run.on_tottime += now - relay->run.on_since;
				relay->run.on_since = 0;
				change = CHTURNOFF;
			}
		}

		// update state time counter
		relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

		// update internal structure
		rwchc_relay_set(&rWCHC_relays, i, relay->run.turn_on);
	}

	// save/log relays state if there was a change
	if (change) {
		hardware_relays_log();
		if (CHTURNOFF == change) {	// only update permanent storage on full cycles (at turn off)
			ret = hardware_save_relays();
			if (ret)
				dbgerr("hardware_save failed (%d)", ret);
		}
	}
	
	// send new state to hardware
	ret = spi_relays_w(&rWCHC_relays);

	// update internal runtime state on success
	// XXX NOTE there will be a discrepancy between internal state and Relays[] if the above fails
	if (ALL_OK == ret)
		Hardware.relays.ALL = rWCHC_relays.ALL;

	return (ret);
}

/**
 * Write all peripherals from internal runtime to hardware
 * @return status
 */
__attribute__((warn_unused_result)) static int hardware_rwchcperiphs_write(void)
{
	if (!Hardware.ready)
		return (-EOFFLINE);
	
	return (spi_peripherals_w(&(Hardware.peripherals)));
}

/**
 * Read all peripherals from hardware into internal runtime
 * @return exec status
 */
__attribute__((warn_unused_result)) static int hardware_rwchcperiphs_read(void)
{
	if (!Hardware.ready)
		return (-EOFFLINE);

	return (spi_peripherals_r(&(Hardware.peripherals)));
}

/**
 * Configure a temperature sensor.
 * @param id the physical id of the sensor to configure (starting from 1)
 * @param type the sensor type (PT1000...)
 * @param name a user-defined name describing the sensor
 * @return exec status
 */
int hardware_sensor_configure(const tempid_t id, const enum e_sensor_type type, const char * const name)
{
	char * str = NULL;

	if (!id || (id > ARRAY_SIZE(Sensors)))
		return (-EINVALID);

	Sensors[id-1].ohm_to_celsius = sensor_o_to_c(type);

	if (!Sensors[id-1].ohm_to_celsius)
		return (-EINVALID);

	if (name) {
		str = strdup(name);
		if (!str)
			return(-EOOM);

		Sensors[id-1].name = str;
	}

	Sensors[id-1].set.type = type;
	Sensors[id-1].set.configured = true;

	return (ALL_OK);
}

/**
 * Deconfigure a temperature sensor.
 * @param id the physical id of the sensor to deconfigure (starting from 1)
 * @return exec status
 */
int hardware_sensor_deconfigure(const tempid_t id)
{
	if (!id || (id > ARRAY_SIZE(Sensors)))
		return (-EINVALID);

	if (!Sensors[id-1].set.configured)
		return (-ENOTCONFIGURED);

	free(Sensors[id-1].name);

	memset(&Sensors[id-1], 0x00, sizeof(Sensors[id-1]));

	return (ALL_OK);
}

/**
 * Validate a temperature sensor for use.
 * Checks that the provided hardware id is valid, that is that it is within boundaries
 * of the hardware limits and the configured number of sensors.
 * Finally it checks that the designated sensor is properly configured in software.
 * @return ALL_OK if sensor is properly configured and available for use.
 */
int hardware_sensor_configured(const tempid_t id)
{
	if (!id || (id > ARRAY_SIZE(Sensors)) || (id > get_runtime()->config->nsensors))
		return (-EINVALID);

	if (!Sensors[id-1].set.configured)
		return (-ENOTCONFIGURED);

	return (ALL_OK);
}

/**
 * Request a hardware relay.
 * Ensures that the desired hardware relay is available and grabs it.
 * @param id target relay id (starting from 1)
 * @param failstate the state assumed by the hardware relay in standalone failover (controlling software failure)
 * @param name the user-defined name for this relay (string will be copied locally)
 * @return exec status
 */
int hardware_relay_request(const relid_t id, const bool failstate, const char * const name)
{
	char * str = NULL;

	if (!id || (id > ARRAY_SIZE(Relays)))
		return (-EINVALID);
	
	if (Relays[id-1].set.configured)
		return (-EEXISTS);

	if (name) {
		str = strdup(name);
		if (!str)
			return(-EOOM);

		Relays[id-1].name = str;
	}

	// register failover state
	rwchc_relay_set(&Hardware.settings.deffail, id-1, failstate);

	Relays[id-1].run.off_since = time(NULL);	// XXX hack

	Relays[id-1].set.configured = true;
	
	return (ALL_OK);
}

/**
 * Release a hardware relay.
 * Frees and cleans up the target hardware relay.
 * @param id target relay id (starting from 1)
 * @return exec status
 */
int hardware_relay_release(const relid_t id)
{
	if (!id || (id > ARRAY_SIZE(Relays)))
		return (-EINVALID);
	
	if (!Relays[id-1].set.configured)
		return (-ENOTCONFIGURED);

	free(Relays[id-1].name);
	
	memset(&Relays[id-1], 0x00, sizeof(Relays[id-1]));
	
	return (ALL_OK);
}

/**
 * set internal relay state (request)
 * @param id id of the internal relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hardware_rwchcrelays_write()
 */
int hardware_relay_set_state(const relid_t id, const bool turn_on, const time_t change_delay)
{
	const time_t now = time(NULL);
	struct s_stateful_relay * relay = NULL;

	if (!id || (id > ARRAY_SIZE(Relays)))
		return (-EINVALID);
	
	relay = &Relays[id-1];
	
	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state state request if delay permits
	if (turn_on) {
		if (!relay->run.is_on) {
			if ((now - relay->run.off_since) < change_delay)
				return (change_delay - (now - relay->run.off_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = true;
		}
	}
	else {	// turn off
		if (relay->run.is_on) {
			if ((now - relay->run.on_since) < change_delay)
				return (change_delay - (now - relay->run.on_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = false;
		}
	}

	return (ALL_OK);
}

/**
 * Get internal relay state (request).
 * Updates run.state_time and returns current state
 * @param id id of the internal relay to modify
 * @return run.is_on
 */
int hardware_relay_get_state(const relid_t id)
{
	const time_t now = time(NULL);
	struct s_stateful_relay * relay = NULL;
	
	if (!id || (id > ARRAY_SIZE(Relays)))
		return (-EINVALID);
	
	relay = &Relays[id-1];
	
	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (relay->run.is_on);
}

/**
 * Firmware version.
 * @return positive version or negative error
 */
int hardware_fwversion(void)
{
	if (!Hardware.ready)
		return (-EOFFLINE);

	return (Hardware.fwversion);
}

/**
 * Get the hardware ready for run loop.
 * Calibrate, then collect and process sensors.
 * @warning can loop forever
 * @return exec status
 */
int hardware_online(void)
{
	struct s_runtime * const runtime = get_runtime();
	int ret;

	if (!runtime->config || !runtime->config->configured)	// for parse_temps()
		return (-ENOTCONFIGURED);

	if (!Hardware.ready)
		return (-EOFFLINE);

	// save settings - for deffail
	ret = hardware_config_store();
	if (ret)
		goto fail;
	
	// calibrate
	ret = hardware_calibrate();
	if (ret)
		goto fail;

	// restore previous state - failure is ignored
	hardware_restore_relays();
	hardware_restore_sensors();

	// read sensors
	ret = hardware_sensors_read(Hardware.sensors);
	if (ret)
		goto fail;

	Hardware.sensors_ftime = time(NULL);

	parse_temps();

fail:
	return (ret);
}

/**
 * Assert that the hardware is ready.
 * @return true if hardware is ready, false otherwise
 */
bool hardware_is_online(void)
{
	return (Hardware.ready);
}

/**
 * Collect inputs from hardware.
 * @note Will process switch inputs.
 * @return exec status
 * @todo review logic
 */
int hardware_input(void)
{
	struct s_runtime * const runtime = get_runtime();
	static rwchc_sensor_t rawsensors[RWCHC_NTSENSORS];
	static unsigned int count = 0, systout = 0;
	static tempid_t tempid = 1;
	static enum e_systemmode cursysmode = SYS_UNKNOWN;
	int ret;
	
	// read peripherals
	ret = hardware_rwchcperiphs_read();
	if (ALL_OK != ret) {
		dbgerr("hardware_rwchcperiphs_read failed (%d)", ret);
		goto out;
	}
	
	// detect hardware alarm condition
	if (Hardware.peripherals.i_alarm) {
		pr_log("Hardware in alarm");
		// clear alarm
		Hardware.peripherals.i_alarm = 0;
		lcd_reset();
		// XXX reset runtime?
	}

	// handle software alarm
	if (alarms_count()) {
		Hardware.peripherals.o_LED2 = 1;
		Hardware.peripherals.o_buzz = !Hardware.peripherals.o_buzz;
		count = 2;
	}
	else {
		Hardware.peripherals.o_LED2 = 0;
		Hardware.peripherals.o_buzz = 0;
	}
	
	// handle switch 1
	if (Hardware.peripherals.i_SW1) {
		Hardware.peripherals.i_SW1 = 0;
		count = 5;
		systout = 3;

		cursysmode++;

		if (cursysmode >= SYS_UNKNOWN)	// last valid mode
			cursysmode = 0;		// first valid mode

		lcd_sysmode_change(cursysmode);	// update LCD
	}

	if (!systout) {
		if (cursysmode != runtime->systemmode) {
			// change system mode
			pthread_rwlock_wrlock(&runtime->runtime_rwlock);
			runtime_set_systemmode(cursysmode);
			pthread_rwlock_unlock(&runtime->runtime_rwlock);
			// hardware_beep()
		}
		cursysmode = runtime->systemmode;
	}
	else
		systout--;

	// handle switch 2
	if (Hardware.peripherals.i_SW2) {
		// increase displayed tempid
		tempid++;
		Hardware.peripherals.i_SW2 = 0;
		count = 5;
		
		if (tempid > runtime->config->nsensors)
			tempid = 1;

		lcd_set_tempid(tempid);	// update sensor
	}
	
	// trigger timed backlight
	if (count) {
		Hardware.peripherals.o_LCDbl = 1;
		if (!--count)
			lcd_fade();	// apply fadeout
	}
	else
		Hardware.peripherals.o_LCDbl = 0;
	
	// calibrate
	ret = hardware_calibrate();
	if (ALL_OK != ret)
		dbgerr("hardware_calibrate failed (%d)", ret);	// flag only: calibrate() will not store invalid values
	
	// read sensors
	ret = hardware_sensors_read(rawsensors);
	if (ALL_OK != ret) {
		// XXX REVISIT: flag the error but do NOT stop processing here
		dbgerr("hardware_sensors_read failed (%d)", ret);
	}
	else {
		// copy valid data to runtime environment
		memcpy(Hardware.sensors, rawsensors, sizeof(Hardware.sensors));
		Hardware.sensors_ftime = time(NULL);
	}
	
	parse_temps();

	ret = ALL_OK;
	
out:
	return (ret);
}

/**
 * Apply commands to hardware.
 * @return exec status
 */
int hardware_output(void)
{
	int ret;
	
	// write relays
	ret = hardware_rwchcrelays_write();
	if (ALL_OK != ret) {
		dbgerr("hardware_rwchcrelays_write failed (%d)", ret);
		goto out;
	}
	
	// write peripherals
	ret = hardware_rwchcperiphs_write();
	if (ALL_OK != ret)
		dbgerr("hardware_rwchcperiphs_write failed (%d)", ret);
	
out:
	return (ret);
}

/**
 * Hardware run loop
 */
int hardware_run(void)
{
	struct s_runtime * const runtime = get_runtime();
	int ret;
	
	if (!runtime->config || !runtime->config->configured || !Hardware.ready) {
		dbgerr("not configured");
		return (-ENOTCONFIGURED);
	}
	
	ret = hardware_input();
	if (ALL_OK != ret)
		goto out;
	
	/* we want to release locks and sleep here to reduce contention and
	 * allow other parts to do their job before writing back */
	//sleep(1);

	// send SPI data
	ret = hardware_output();
	
out:
	return (ret);
}

/**
 * Hardware offline routine.
 * Forcefully turns all relays off and saves final counters to permanent storage.
 * @return exec status
 */
int hardware_offline(void)
{
	uint_fast8_t i;
	int ret;
	
	// turn off each known hardware relay
	for (i=0; i<ARRAY_SIZE(Relays); i++) {
		if (!Relays[i].set.configured)
			continue;
		
		Relays[i].run.turn_on = false;
	}
	
	// update the hardware
	ret = hardware_rwchcrelays_write();
	if (ret)
		dbgerr("hardware_rwchcrelays_write failed (%d)", ret);
	
	// update permanent storage with final count
	hardware_save_relays();

	hardware_save_sensors();
	
	Hardware.ready = false;
	
	return (ret);
}

/**
 * Hardware exit routine.
 * Resets the hardware.
 * @warning RESETS THE HARDWARE: no hardware operation after that call.
 */
void hardware_exit(void)
{
	int ret;
	uint_fast8_t i;

	// cleanup all resources
	for (i = 1; i <= ARRAY_SIZE(Relays); i++)
		hardware_relay_release(i);
	
	// deconfigure all sensors
	for (i = 1; i <= ARRAY_SIZE(Sensors); i++)
		hardware_sensor_deconfigure(i);

	// reset the hardware
	ret = spi_reset();
	if (ret)
		dbgerr("reset failed (%d)", ret);
}