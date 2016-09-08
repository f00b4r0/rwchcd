//
//  rwchcd_hardware.c
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#include <time.h>

#include "rwchcd.h"
#include "rwchcd_spi.h"
#include "rwchcd_hardware.h"


/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @param last the id of the last wanted (connected) sensor
 */
static int sensors_read(uint16_t tsensors[], const int last)
{
	int sensor, ret = -1;

	for (sensor=0; sensor<last; sensor++) {
		if (rwchcd_spi_sensor_r(tsensors, sensor))
			goto out;
	}

	if (rwchcd_spi_sensor_r(tsensors, RWCHC_NTSENSORS-1))	// grab reference
		goto out;

	ret = 0;
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

static void calibrate(void)
{
	struct s_runtime * const runtime = get_runtime();
	int refcalib, ref;

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

/*
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
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

static temp_t ohm_to_temp(const unsigned int ohm)
{
	const float R0 = 1000.0;
	float alpha, beta, delta, A, B, C, temp;

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

	return (celsius_to_temp(temp));
}

/**
 * set internal relay state
 * @param relay the internal relay to modify
 * @param state the desired target state
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @todo time management should really be in the routine that writes to the hardware for maximum accuracy
 */
int set_relay_state(struct s_stateful_relay * relay, bool turn_on, time_t change_delay)
{
	const struct s_runtime * const runtime = get_runtime();
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
		setbit(runtime->rWCHC_relays->ALL, rid);
	else
		clrbit(runtime->rWCHC_relays->ALL, rid);

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

/**
 * Set pump state.
 * @param pump target pump
 * @param state target pump state
 * @param force_state skips cooldown if true
 * @return error code if any
 */
int set_pump_state(struct s_pump * const pump, bool state, bool force_state)
{
	time_t cooldown = 0;	// by default, no wait

	if (!pump)
		return (-EINVALID);

	if (!pump->configured)
		return (-ENOTCONFIGURED);

	// apply cooldown to turn off, only if not forced.
	// If ongoing cooldown, resume it, otherwise restore default value
	if (!state && !force_state)
		cooldown = pump->actual_cooldown_time ? pump->actual_cooldown_time : pump->set_cooldown_time;

	// XXX this will add cooldown everytime the pump is turned off when it was already off but that's irrelevant
	pump->actual_cooldown_time = set_relay_state(pump->relay, state, cooldown);
}

int get_pump_state(const struct s_pump * const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->configured)
		return (-ENOTCONFIGURED);
	
	// XXX we could return remaining cooldown time if necessary
	return (get_relay_state(pump->relay));
}
