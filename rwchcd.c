//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

//* TODO
// Setup
// Computation
// Control
// accounting (separate module that periodically polls states and write them to timestamped registry)
// Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
// UI + programming
// handle summer switchover
// connection of multiple instances

// http://www.energieplus-lesite.be/index.php?id=10963


#include <stdio.h>
#include <unistd.h>	// sleep/usleep
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include "rwchcd.h"

static struct s_runtime Runtime;

inline struct s_runtime * get_runtime(void)
{
	return (&Runtime);
}

static void parse_temps(void)
{
	struct s_runtime * const runtime = get_runtime();
	int i;

	for (i = 0; i<runtime->config->nsensors; i++) {
		runtime->temps[i] = ohm_to_temp(sensor_to_ohm(runtime->rWCHC_sensors[i], 1));
	}
}

/**
 * get temp from a given temp id
 * @return temp if id valid, 0 otherwise
 */
static temp_t get_temp(const tempid_t id)
{
	const struct s_runtime * const runtime = get_runtime();

	if (id > runtime->config->nsensors)
		return (0);

	return (runtime->temps[id]);	// XXX REVISIT lock
}

short validate_temp(const temp_t temp)
{
	int ret = ALL_OK;

	if (temp == 0)
		ret = -ESENSORINVAL;
	else if (temp <= RWCHCD_TEMPMIN)
		ret = -ESENSORSHORT;
	else if (temp >= RWCHCD_TEMPMAX)
		ret = -ESENSORDISCON;

	return (ret);
}

static inline temp_t celsius_to_temp(const float celsius)
{
	return ((temp_t)(celsius + 273.15)*100);
}

static inline float temp_to_celsius(const temp_t temp)
{
	return ((float)((float)temp/100.0 - 273.15));
}

/**
 * Implement time-based PI controller in velocity form
 * Saturation : max = boiler temp, min = return temp
 * We want to output
 http://www.plctalk.net/qanda/showthread.php?t=19141
 // http://www.energieplus-lesite.be/index.php?id=11247
 // http://www.ferdinandpiette.com/blog/2011/08/implementer-un-pid-sans-faire-de-calculs/
 // http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/
 // http://controlguru.com/process-gain-is-the-how-far-variable/
 // http://www.rhaaa.fr/regulation-pid-comment-la-regler-12
 // http://controlguru.com/the-normal-or-standard-pid-algorithm/
 // http://www.csimn.com/CSI_pages/PIDforDummies.html
 // https://en.wikipedia.org/wiki/PID_controller
 */
static float control_PI(const float target, const float actual)
{
	float Kp, Ki;	// XXX PID settings
	float error, error_prev, error_change, output, iterm;
	static float iterm_prev, error_prev, output_prev, prev;

	error = target - actual;

	// Integral term
	iterm = Ki * error;

	// Proportional term
	pterm = Kp * (prev - actual);
	prev = actual;

	output = iterm + pterm + output_prev;
	output_prev = output;

	return (output);
}

static bool overtemp_protection(const struct s_boiler * const boiler,
				const struct s_valve * const mixer,
				const struct s_stateful_relay * const pump)
{
	static bool tripped = false;
	float triptemp;

	triptemp = boiler->limit_tmax;
	if (tripped)
		triptemp -= boiler->histeresis * 2;	// XXX untrip at histeresis *2

	if (boiler->temp > triptemp) {
		tripped = true;

		// stop boiler
		set_relay_state(boiler->burner_1, OFF, 0);

		// start pump
		set_relay_state(pump, ON, 0);

		// open valve
		set_mixer_pos(mixer, 100);
	}
	else
		tripped = false;

	return (tripped);
}

/**
 * Exponentially weighted moving average implementing a trivial LP filter
 http://www.rowetel.com/blog/?p=1245
 https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 */
static float expw_mavg(float filtered, float new_sample, time_t tau, time_t dt)
{
	float alpha = dt / (tau+dt);	// dt sampling itvl, tau = constante de temps

	return (filtered - (alpha * (filtered - new_sample)));
}

static void outdoor_temp()
{
	static time_t lasttime = time(NULL);
	const struct s_runtime * const runtime = get_runtime();
	const time_t dt = time(NULL) - lasttime;
	lasttime = time(NULL);

	runtime->t_outdoor = get_temp(runtime->config->id_temp_outdoor);	// XXX checks
	runtime->t_outdoor_mixed = expw_mavg(runtime->t_outdoor_mixed, runtime->t_outdoor, runtime->config->building_tau, dt);
	runtime->t_outdoor_attenuated = expw_mavg(runtime->t_outdoor_attenuated, runtime->t_outdoor_mixed, runtime->config->building_tau, dt);
}

static int init_process()
{
	t_outdoor = t_outdoor_mixed = t_outdoor_attenuated = *(Config.temp_outdoor);

	// set mixing valve to known start state
	set_mixer_pos(&Valve, -1);	// force fully closed during more than normal ete_time

}

/*
 temp conversion from sensor raw value + calibration
 temp boiler: target water temp + hist; ceil and floor
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
