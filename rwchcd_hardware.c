//
//  rwchcd_hardware.c
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include <time.h>	// time
#include <math.h>	// sqrtf
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <unistd.h>	// sleep

#include "rwchcd.h"
#include "rwchcd_spi.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_storage.h"
#include "rwchcd_hardware.h"

#define RELAY_MAX_ID	14	///< maximum valid relay id

#define VALID_CALIB_MIN	0.8F
#define VALID_CALIB_MAX	1.2F

static const storage_version_t Hardware_sversion = 1;
static struct s_stateful_relay * Relays[RELAY_MAX_ID];	///< physical relays

/**
 * Convert sensor value to actual resistance.
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
 * @param raw the raw sensor value
 * @param calib 1 if calibrated value is required, 0 otherwise
 * @return the resistance value
 */
static unsigned int sensor_to_ohm(const rwchc_sensor_t raw, const bool calib)
{
	const struct s_runtime * const runtime = get_runtime();
	const uint_fast16_t dacset[] = RWCHC_DAC_STEPS;
	uint_fast16_t value, dacoffset;
	float calibmult;

	dacoffset = (raw >> 12) & 0x3;

	value = raw & RWCHC_ADC_MAXV;		// raw is 10bit, cannot be negative when cast to sint
	value *= RWCHC_ADC_MVSCALE;		// convert to millivolts
	value += dacset[dacoffset]*RWCHC_DAC_MVSCALE*RWCHC_ADC_OPGAIN;	// add the initial offset

	/* value is now (1+RWCHC_ADC_OPGAIN) * actual value at sensor. Sensor is fed 0.5mA,
	 * so sensor resistance is 1/2 actual value in millivolt. 1+RWCHC_ADC_OPGAIN = 4.
	 * Thus, resistance in ohm is value/2 */

	value /= 2;

	// finally, apply calibration factor
	if (calib)
		calibmult = dacoffset ? runtime->calib_dac : runtime->calib_nodac;
	else
		calibmult = 1.0;

	value = ((float)value * calibmult);	// calibrate

	return (value);
}

// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

/**
 * Convert Pt1000 resistance value to actual temperature.
 * Use a quadratic fit for simplicity.
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
static float pt1000_ohm_to_celsius(const uint_fast16_t ohm)
{
	const float R0 = 1000.0F;
	float alpha, delta, A, B, temp;

	// manufacturer parameters
	alpha = 0.003850F;	// mean R change referred to 0C
	//beta = 0.10863F;
	delta = 1.4999F;

	// Callendar - Van Dusen parameters
	A = alpha + (alpha * delta) / 100;
	B = (-alpha * delta) / (100 * 100);
	//C = (-alpha * beta) / (100 * 100 * 100 * 100);	// only for t < 0

	// quadratic fit: we're going to ignore the cubic term given the temperature range we're looking at
	temp = (-R0*A + sqrtf(R0*R0*A*A - 4*R0*B*(R0 - ohm))) / (2*R0*B);

	return (temp);
}

/**
 * Return a calibrated temp_t value for the given raw sensor data.
 * @param raw the raw sensor data to convert
 * @return the temperature in temp_t units
 * XXX REVISIT calls depth.
 */
temp_t sensor_to_temp(const rwchc_sensor_t raw)
{
	return (celsius_to_temp(pt1000_ohm_to_celsius(sensor_to_ohm(raw, 1))));
}

/**
 * Save hardware state to permanent storage
 * @return exec status
 */
static int hardware_save(void)
{
	static uint8_t blob[ARRAY_SIZE(Relays)*sizeof(*Relays[0])];
	unsigned int i;
	
	for (i=0; i<ARRAY_SIZE(Relays); i++) {
		if (Relays[i])
			memcpy(&blob[i*sizeof(*Relays[0])], Relays[i], sizeof(*Relays[0]));
		else
			memset(&blob[i*sizeof(*Relays[0])], 0x00, sizeof(*Relays[0]));
	}
	
	return (storage_dump("hardware", &Hardware_sversion, &blob, sizeof(blob)));
}

/**
 * Restore hardware state from permanent storage
 * Restores cycles and on/off total time counts for set relays.
 * @return exec status
 */
static int hardware_restore(void)
{
	static uint8_t blob[ARRAY_SIZE(Relays)*sizeof(*Relays[0])];
	storage_version_t sversion;
	typeof(Relays[0]) relayptr = (typeof(relayptr))&blob;
	unsigned int i;
	
	// try to restore key elements of hardware
	if (storage_fetch("hardware", &sversion, blob, sizeof(blob)) == ALL_OK) {
		if (Hardware_sversion != sversion)
			return (ALL_OK);	// XXX

		for (i=0; i<ARRAY_SIZE(Relays); i++) {
			if (Relays[i]) {
				Relays[i]->run.on_tottime += relayptr->run.on_tottime;
				Relays[i]->run.off_tottime += relayptr->run.off_tottime;
				Relays[i]->run.cycles += relayptr->run.cycles;
			}
			relayptr++;
		}
	}
	else
		dbgmsg("storage_fetch failed");

	return (ALL_OK);
}

/**
 * Initialize hardware and ensure connection is set
 * @return error state
 */
int hardware_init(void)
{
	if (rwchcd_spi_init() < 0)
		return (-EINIT);

	memset(Relays, 0x0, ARRAY_SIZE(Relays));

	return (ALL_OK);
}

/**
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * @return error status
 * @note rwchcd_spi_calibrate() sleeps so this will sleep too, up to RWCHCD_SPI_MAX_TRIES times
 */
static int hardware_calibrate(void)
{
	struct s_runtime * const runtime = get_runtime();
	uint_fast16_t refcalib, i;
	int ret = ALL_OK;
	rwchc_sensor_t ref;

	i = 0;
	do {
		ret = rwchcd_spi_calibrate();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	i = 0;
	do {
		ret = rwchcd_spi_ref_r(&ref, 0);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		runtime->calib_nodac = (1000.0F / (float)refcalib);	// calibrate against 1kohm reference
	}

	if ((runtime->calib_nodac < VALID_CALIB_MIN) || (runtime->calib_nodac > VALID_CALIB_MAX)) {
		ret = -EGENERIC;
		goto out;	// XXX should not happen
	}

	i = 0;
	do {
		ret = rwchcd_spi_ref_r(&ref, 1);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		runtime->calib_dac = (1000.0F / (float)refcalib);	// calibrate against 1kohm reference
	}

	if ((runtime->calib_dac < VALID_CALIB_MIN) || (runtime->calib_dac > VALID_CALIB_MAX))
		ret = -EGENERIC;	// XXX should not happen

out:
	return (ret);
}

/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @param last the id of the last wanted (connected) sensor
 */
int hardware_sensors_read(rwchc_sensor_t tsensors[], const int_fast16_t last)
{
	int_fast8_t sensor;
	int i, ret = ALL_OK;

	if (last > RWCHC_NTSENSORS)
		return (-EINVALID);

	for (sensor=0; sensor<last; sensor++) {
		i = 0;
		do {
			ret = rwchcd_spi_sensor_r(tsensors, sensor);
		} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

		if (ret)
			goto out;
	}

out:
	return (ret);
}

/**
 * Write all relays
 * This function updates all known hardware relays according to their desired turn_on
 * state. This function also does time and cycle accounting for the relays.
 * @note non-configured hardware relays are turned off.
 * @return status
 */
int hardware_rwchcrelays_write(void)
{
	struct s_runtime * const runtime = get_runtime();
	struct s_stateful_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const time_t now = time(NULL);	// we assume the whole thing will take much less than a second
	uint_fast8_t rid, i;
	int ret = -EGENERIC;

	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<RELAY_MAX_ID; i++) {
		relay = Relays[i];

		if (!relay)
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
			}
		}
		else {	// turn off
			if (relay->run.is_on) {	// relay is currently on
				relay->run.is_on = false;
				relay->run.off_since = now;
				if (relay->run.on_since)
					relay->run.on_tottime += now - relay->run.on_since;
				relay->run.on_since = 0;
			}
		}

		// update state time counter
		relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

		// extract relay id XXX REVISIT
		rid = relay->set.id - 1;
		if (rid > 6)
			rid++;	// skip the hole

		// set state for triac control
		if (relay->run.turn_on)
			setbit(rWCHC_relays.ALL, rid);
		else
			clrbit(rWCHC_relays.ALL, rid);
	}

	// send new state to hardware
	i = 0;
	do {
		ret = rwchcd_spi_relays_w(&rWCHC_relays);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	// update internal runtime state on success
	// XXX NOTE there will be a discrepancy between internal state and Relays[] if the above fails
	if (ALL_OK == ret)
		runtime->rWCHC_relays.ALL = rWCHC_relays.ALL;

	return (ret);
}

/**
 * Write all peripherals from internal runtime to hardware
 * @return status
 */
int hardware_rwchcperiphs_write(void)
{
	const struct s_runtime * const runtime = get_runtime();
	int i = 0, ret;

	do {
		ret = rwchcd_spi_peripherals_w(&(runtime->rWCHC_peripherals));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Read all peripherals from hardware into internal runtime
 * @return exec status
 */
int hardware_rwchcperiphs_read(void)
{
	struct s_runtime * const runtime = get_runtime();
	int i = 0, ret;

	do {
		ret = rwchcd_spi_peripherals_r(&(runtime->rWCHC_peripherals));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Create a new stateful relay
 * @return pointer to the created relay
 */
struct s_stateful_relay * hardware_relay_new(void)
{
	struct s_stateful_relay * const relay = calloc(1, sizeof(struct s_stateful_relay));

	// at creation relay is off
	if (relay)
		relay->run.off_since = time(NULL);

	return (relay);
}

/**
 * Delete a stateful relay.
 * @param relay the relay to delete
 * @note the deleted relay will be turned off by a call to _write()
 */
void hardware_relay_del(struct s_stateful_relay * relay)
{
	if (!relay)
		return;

	Relays[relay->set.id-1] = NULL;

	free(relay->name);
	relay->name = NULL;
	free(relay);
}

/**
 * Set a relay's id
 * @param relay the target relay
 * @param id the considered hardware id (numbered from 1)
 * @return exec status
 */
int hardware_relay_set_id(struct s_stateful_relay * const relay, const uint_fast8_t id)
{
	if (!relay)
		return (-EINVALID);

	if (!id || id > RELAY_MAX_ID)
		return (-EINVALID);

	if (Relays[id-1])
		return (-EEXISTS);

	relay->set.id = id;
	Relays[id-1] = relay;

	return (ALL_OK);
}

/**
 * set internal relay state (request)
 * @param relay the internal relay to modify
 * @param state the desired target state
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hardware_rwchcrelays_write()
 */
int hardware_relay_set_state(struct s_stateful_relay * const relay, const bool turn_on, const time_t change_delay)
{
	const time_t now = time(NULL);

	if (!relay)
		return (-EINVALID);

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

int hardware_relay_get_state(struct s_stateful_relay * const relay)
{
	const time_t now = time(NULL);

	if (!relay)
		return (-EINVALID);

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (relay->run.is_on);
}

/**
 * Get the hardware ready for run loop.
 * Calibrate, then collect and process sensors.
 * @return exec status
 */
int hardware_online(void)
{
	struct s_runtime * const runtime = get_runtime();
	int ret;

	if (!runtime->config || !runtime->config->configured)
		return (-ENOTCONFIGURED);

	// calibrate
	ret = hardware_calibrate();
	if (ret)
		goto fail;
	
	// restore previous state
	ret = hardware_restore();
	if (ret)
		goto fail;

	// read sensors
	ret = hardware_sensors_read(runtime->rWCHC_sensors, runtime->config->nsensors);
	if (ret)
		goto fail;

fail:
	return (ret);
}

/**
 * Hardware run loop
 */
void hardware_run(void)
{
	struct s_runtime * const runtime = get_runtime();
	static rwchc_sensor_t rawsensors[RWCHC_NTSENSORS];
	int ret;

	if (!runtime->config || !runtime->config->configured) {
		dbgerr("not configured");
		return;	// XXX when this is a while(1){} thread this should be 'continue'
	}

	// fetch SPI data

#if 0
	// read peripherals
	ret = hardware_rwchcperiphs_read();
	if (ret)
		dbgerr("hardware_rwchcperiphs_read failed (%d)", ret);
#endif

	// read sensors
	ret = hardware_sensors_read(rawsensors, runtime->config->nsensors);
	if (ret) {
		// XXX REVISIT: flag the error but do NOT stop processing here
		dbgerr("hardware_sensors_read failed: %d", ret);
	}
	else {
		// copy valid data to runtime environment
		memcpy(runtime->rWCHC_sensors, rawsensors, sizeof(runtime->rWCHC_sensors));
	}

	/* we want to release locks and sleep here to reduce contention and
	 * allow other parts to do their job before writing back */
	//sleep(1);

	// send SPI data

	// write relays
	ret = hardware_rwchcrelays_write();
	if (ret)
		dbgerr("hardware_rwchcrelays_write failed: %d", ret);

#if 0
	// write peripherals
	ret = hardware_rwchcperiphs_write();
	if (ret)
		dbgerr("hardware_rwchcperiphs_write failed (%d)", ret);
#endif
	
	// save state
	ret = hardware_save();
	if (ret)
		dbgerr("hardware_save failed (%d)", ret);
}