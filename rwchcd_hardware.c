//
//  rwchcd_hardware.c
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#include <time.h>
#include <math.h>	// sqrtf

#include "rwchcd.h"
#include "rwchcd_spi.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"


/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @param last the id of the last wanted (connected) sensor
 */
int hardware_sensors_read(uint16_t tsensors[], const int last)
{
	int sensor, i, ret = ALL_OK;

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
int hardware_relays_write(const union rwchc_u_relays * const relays)
{
	int i = 0, ret = ALL_OK;

	if (!relays)
		return (-EINVALID);

	do {
		ret = rwchcd_spi_relays_w(relays);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

out:
	return (ret);
}

/**
 * Write all peripherals
 * @param periphs pointer to the harware periphs union
 * @return status
 */
int hardware_periphs_write(const union rwchc_u_outperiphs * const periphs)
{
	int i = 0, ret = ALL_OK;

	if (!periphs)
		return (-EINVALID);

	do {
		ret = rwchcd_spi_peripherals_w(periphs);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

out:
	return (ret);
}

/**
 * Write a string to LCD.
 * @warning No boundary checks
 * @param str string to send
 * @return error code
 */
static int lcd_wstr(const char * str)
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
static unsigned int sensor_to_ohm(const uint16_t raw, const int calib)
{
	const struct s_runtime * const runtime = get_runtime();
	const unsigned int dacset[] = {0, 64, 128, 255};
	unsigned int value, dacoffset;
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
static float ohm_to_celsius(const unsigned int ohm)
{
	const float R0 = 1000.0;
	float alpha, delta, A, B, temp;

	// manufacturer parameters
	alpha = 0.003850;	// mean R change referred to 0C
	//beta = 0.10863;
	delta = 1.4999;

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
 */
static void calibrate(void)
{
	struct s_runtime * const runtime = get_runtime();
	int refcalib;
	uint16_t ref;

	while (rwchcd_spi_ref_r(&ref, 0));

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		runtime->calib_nodac = (1000.0 / (float)refcalib);	// calibrate against 1kohm reference
	}

	while (rwchcd_spi_ref_r(&ref, 1));

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		runtime->calib_dac = (1000.0 / (float)refcalib);	// calibrate against 1kohm reference
	}
}

/**
 * Return a calibrated temp_t value for the given raw sensor data.
 * @param raw the raw sensor data to convert
 * @return the temperature in temp_t units
 * XXX REVISIT calls depth.
 */
temp_t sensor_to_temp(const uint16_t raw)
{
	return (celsius_to_temp(ohm_to_celsius(sensor_to_ohm(raw, 1))));
}

/**
 * set internal relay state
 * @param relay the internal relay to modify
 * @param state the desired target state
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @todo time management should really be in the routine that writes to the hardware for maximum accuracy
 */
int set_relay_state(struct s_stateful_relay * const relay, const bool turn_on, const time_t change_delay)
{
	struct s_runtime * const runtime = get_runtime();
	const time_t now = time(NULL);
	unsigned short rid;

	if (!relay)
		return (-EINVALID);

	if (!relay->configured)
		return (-ENOTCONFIGURED);

	// account for state time
	if (turn_on) {
		if (!relay->is_on) {
			if ((now - relay->off_since) < change_delay)
				return (change_delay - (now - relay->off_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->cycles++;	// increment cycle count
			relay->is_on = true;
			relay->on_since = now;
			relay->off_time += now - relay->off_since;
		}
	}
	else {	// OFF == state
		if (relay->is_on) {
			if ((now - relay->on_since) < change_delay)
				return (change_delay - (now - relay->on_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->is_on = false;
			relay->off_since = now;
			relay->on_time += now - relay->on_since;
		}
	}

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

int get_relay_state(const struct s_stateful_relay * const relay)
{
	if (!relay)
		return (-EINVALID);

	if (!relay->configured)
		return (-ENOTCONFIGURED);

	return (relay->is_on);
}