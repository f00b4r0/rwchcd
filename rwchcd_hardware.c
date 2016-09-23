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

#include "rwchcd.h"
#include "rwchcd_spi.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"

#define RELAY_MAX_ID	14	///< maximum valid relay id

static struct s_stateful_relay * Relays[RELAY_MAX_ID];

/**
 * Write a string to LCD.
 * @warning No boundary checks
 * @param str string to send
 * @return error code
 */
int lcd_wstr(const char * str)
{
	int ret = -1;

	while (*str != '\0') {
		if (rwchcd_spi_lcd_data_w(*str))
			goto out;
		str++;
		//usleep(100); DISABLED: SPI_rw8bit() already sleeps
	}

	ret = 0;
out:
	return (ret);
}

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
	const uint_fast16_t dacset[] = {0, 64, 128, 255};
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
 * Convert resistance value to actual temperature.
 * Use a quadratic fit for simplicity.
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
static float ohm_to_celsius(const uint_fast16_t ohm)
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
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * @return error status
 */
static int calibrate(void)
{
	struct s_runtime * const runtime = get_runtime();
	uint_fast16_t refcalib, i;
	int ret = ALL_OK;
	rwchc_sensor_t ref;

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

out:
	return (ret);
}

/**
 * Return a calibrated temp_t value for the given raw sensor data.
 * @param raw the raw sensor data to convert
 * @return the temperature in temp_t units
 * XXX REVISIT calls depth.
 */
temp_t sensor_to_temp(const rwchc_sensor_t raw)
{
	return (celsius_to_temp(ohm_to_celsius(sensor_to_ohm(raw, 1))));
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

	return (calibrate());
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
 * @param relays pointer to the harware relay union
 * @return status
 */
int hardware_rwchcrelays_write(const union rwchc_u_relays * const relays)
{
	int i = 0, ret;

	if (!relays)
		return (-EINVALID);

	do {
		ret = rwchcd_spi_relays_w(relays);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Write all peripherals
 * @param periphs pointer to the harware periphs union
 * @return status
 */
int hardware_rwchcperiphs_write(const union rwchc_u_outperiphs * const periphs)
{
	int i = 0, ret;

	if (!periphs)
		return (-EINVALID);

	do {
		ret = rwchcd_spi_peripherals_w(periphs);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Read all peripherals
 * @param periphs pointer to the hardware periphs union
 * @return exec status
 */
int hardware_rwchcperiphs_read(union rwchc_u_outperiphs * const periphs)
{
	int i = 0, ret;

	if (!periphs)
		return (-EINVALID);

	do {
		ret = rwchcd_spi_peripherals_r(periphs);
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
		relay->off_since = time(NULL);

	return (relay);
}

/**
 * Delete a stateful relay.
 * Turns off the relay before removing it from the system.
 * @param relay the relay to delete
 */
void hardware_relay_del(struct s_stateful_relay * relay)
{
	if (!relay)
		return;

	// turn off the relay first
	hardware_relay_set_state(relay, OFF, 0);

	Relays[relay->id-1] = NULL;

	free(relay->name);
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

	relay->id = id;
	Relays[id-1] = relay;

	return (ALL_OK);
}

/**
 * set internal relay state
 * @param relay the internal relay to modify
 * @param state the desired target state
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @todo time management should really be in the routine that writes to the hardware for maximum accuracy
 XXX REVIEW LATE CODE
 */
int hardware_relay_set_state(struct s_stateful_relay * const relay, const bool turn_on, const time_t change_delay)
{
	struct s_runtime * const runtime = get_runtime();
	const time_t now = time(NULL);
	uint_fast8_t rid;

	if (!relay)
		return (-EINVALID);

	if (!relay->configured)
		return (-ENOTCONFIGURED);

	// update state counters at state change
	if (turn_on) {
		if (!relay->is_on) {
			if ((now - relay->off_since) < change_delay)
				return (change_delay - (now - relay->off_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->cycles++;	// increment cycle count
			relay->is_on = true;
			relay->on_since = now;
			if (relay->off_since)
				relay->off_tottime += now - relay->off_since;
			relay->off_since = 0;
		}
	}
	else {	// turn off
		if (relay->is_on) {
			if ((now - relay->on_since) < change_delay)
				return (change_delay - (now - relay->on_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->is_on = false;
			relay->off_since = now;
			if (relay->on_since)
				relay->on_tottime += now - relay->on_since;
			relay->on_since = 0;
		}
	}

	// update state time counter
	relay->state_time = relay->is_on ? (now - relay->on_since) : (now - relay->off_since);

	// extract relay id XXX REVISIT
	rid = relay->id - 1;
	if (rid > 6)
		rid++;	// skip the hole

	// set state for triac control
	if (turn_on)
		setbit(runtime->rWCHC_relays.ALL, rid);
	else
		clrbit(runtime->rWCHC_relays.ALL, rid);

	return (ALL_OK);
}

int hardware_relay_get_state(struct s_stateful_relay * const relay)
{
	const time_t now = time(NULL);

	if (!relay)
		return (-EINVALID);

	if (!relay->configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->state_time = relay->is_on ? (now - relay->on_since) : (now - relay->off_since);

	return (relay->is_on);
}