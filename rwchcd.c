//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

// Setup
// Computation
// Control + accounting
// UI + programming


#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include <math.h>
#include "rwchcd_spi.h"


static uint16_t TSENSORS[RWCHC_NTSENSORS];
static union u_relays rWCHC_relays;
static union u_outperiphs rWCHC_peripherals;
static struct s_settings rWCHC_settings;

static float calib_nodac = 1, calib_dac = 1;

/**
 * Read all sensors
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
	return ret;
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
	return ret;
}

/*
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
 */
static unsigned int sensor_to_ohm(const uint16_t raw, const int calib)
{
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
		calibmult = dacoffset ? calib_dac : calib_nodac;
	else
		calibmult = 1.0;

	value = ((float)value * calibmult);	// calibrate

	return (value);
}

static void calibrate(void)
{
	int refcalib, ref;

	while (rwchcd_spi_ref_r(&ref, 0));

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		calib_nodac = (1000.0 / (float)refcalib);	// calibrate against 1kohm reference
	}

	while (rwchcd_spi_ref_r(&ref, 1));

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		calib_dac = (1000.0 / (float)refcalib);	// calibrate against 1kohm reference
	}
}

// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

static float ohm_to_temp(const unsigned int ohm)
{
	float alpha, beta, delta, A, B, C, temp;
#define	R0 1000.0

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

	return temp;
}

/*
 Loi d'eau linaire: pente + offset
 pente calculee negative puisqu'on conserve l'axe des abscisses dans la bonne orientation
 */
static float loi_deau(const float ext_temp)
{
	float out_temp1 = -5.0, water_temp1 = 50.0, out_temp2 = 15.0, water_temp2 = 30.0; // XXX settings
	float slope, offset;

	// (Y2 - Y1)/(X2 - X1)
	slope = (water_temp2 - water_temp1) / (out_temp2 - out_temp1);
	// reduction par un point connu
	offset = water_temp2 - (out_temp2 * slope);

	// Y = input*slope + offset
	return (ext_temp * slope + offset);
}

// http://www.ferdinandpiette.com/blog/2011/08/implementer-un-pid-sans-faire-de-calculs/
static float control_PID(const float target, const float actual)
{
	float Kp, Ki, Kd;	// XXX PID settings
	float error, error_prev, error_change, output;
	static float error_sum;	// XXX will overflow. Implement windowed sum with circular buffer

	error = target - actual;
	error_sum += error;
	error_change = error - error_prev;

	output = Kp * error + Ki * error_sum + Kd * error_change;
	error_prev = error;

	return (output);
}

/*
 temp conversion from sensor raw value + calibration
 temp burner: target water temp + hist; ceil and floor
 temp water: water curve with outdoor temp + timing (PID?) comp (w/ building constant) + indoor comp
 valve position: PID w/ total run time from C to O
 */

int main(void)
{
	int i, ret;
	
	if (rwchcd_spi_init() < 0)
		printf("init error\n");
	
	 ret = rwchcd_spi_lcd_acquire();
	 printf("rwchcd_spi_lcd_acquire: %d\n", ret);
	 
	 ret = rwchcd_spi_lcd_cmd_w(0x1);	// clear
	 printf("rwchcd_spi_lcd_cmd_w: %d\n", ret);
	 sleep(2);
	 ret = lcd_wstr("Hello!");
	 printf("lcd_wstr: %d\n", ret);
	 
	 ret = rwchcd_spi_lcd_relinquish();
	 printf("rwchcd_spi_lcd_relinquish: %d\n", ret);
	
	rWCHC_peripherals.LCDbl = 0;
	ret = rwchcd_spi_peripherals_w(&rWCHC_peripherals);
	printf("rwchcd_spi_peripherals_w: %d\n", ret);

#define S rWCHC_settings
	while (1) {
		//i = rwchcd_spi_peripherals_r();
		//printf("periph byte: %d, ret: %d\n", rWCHC_peripherals.BYTE, i);
		i=0;
		/*
		 ret = rwchcd_spi_settings_r(&rWCHC_settings);
		 printf("rwchcd_spi_settings_r: %d\n", ret);
		 printf("settings: %d; %d, %d, %d, %d;\n"
		 "\t%x, %x, %x, %x, %x, %x, %x, %x\n",
		 S.lcdblpct, S.limits.burner_tmax, S.limits.burner_tmin,
		 S.limits.water_tmin, S.limits.frost_tmin,
		 S.addresses.T_burner, S.addresses.T_pump,
		 S.addresses.T_Vopen, S.addresses.T_Vclose,
		 S.addresses.S_burner, S.addresses.S_water,
		 S.addresses.S_outdoor, S.addresses.nsensors);
		 */
		//		rWCHC_settings.lcdblpct = 30;
		
		/*		do {
			ret = rwchcd_spi_settings_w(&rWCHC_settings);
			printf("rwchcd_spi_settings_w: %d\n", ret);
		 } while (ret);
		 //ret = rwchcd_spi_settings_s();
		 //printf("rwchcd_spi_settings_s: %d\n", ret);
		 */

	/*	 for (i=0; i<128; i++) {
			 //	ret = rwchcd_spi_lcd_bl_w(i);
			 //printf("rwchcd_spi_lcd_bl_w: %d\n", ret);
		 
			rWCHC_relays.LOWB = i;
			rWCHC_relays.HIGHB= i;
		 again:
			printf("LOWB: %x, HIGHB: %x\n", rWCHC_relays.LOWB, rWCHC_relays.HIGHB);
			ret = rwchcd_spi_relays_w(&rWCHC_relays);
			printf("rwchcd_spi_relays_w: %d\n", ret);
			if (ret) {
				printf("TRYING AGAIN!\n");
				goto again;
			}
			 //rWCHC_peripherals.LCDbl = (i&1);
			 //ret = rwchcd_spi_peripherals_w(&rWCHC_peripherals);
			 //printf("rwchcd_spi_peripherals_w: %d\n", ret);
			sleep(1);
		 } */
		//for (i=100; i>0; i--)
		//	rwchcd_spi_lcd_bl_w(i);

		ret = sensors_read(TSENSORS, 0xF);
		printf("sensors_read: %d\n", ret);
		if (!ret)
			for (i=0; i<RWCHC_NTSENSORS; i++)
				printf("sensor %d: %d\n", i, TSENSORS[i]);

		calibrate();
		printf("temp_calib 0: %f, 1: %f\n", calib_nodac, calib_dac);
		printf("sensor 1 ohm: %d\n", sensor_to_ohm(TSENSORS[1], 1));
		printf("sensor 4 ohm: %d\n", sensor_to_ohm(TSENSORS[4], 1));
		printf("sensor 7 ohm: %d\n", sensor_to_ohm(TSENSORS[7], 1));
		printf("sensor 15 temp: %f\n", ohm_to_temp(sensor_to_ohm(TSENSORS[15], 1)));
		sleep(5);
	}
}
