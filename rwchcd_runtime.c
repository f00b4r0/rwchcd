//
//  rwchcd_runtime.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#include <time.h>	// time_t
#include <string.h>	// memset/memcpy
#include <math.h>	// roundf
#include "rwchcd_lib.h"
#include "rwchcd_plant.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lcd.h"
#include "rwchcd_hardware.h"	// sensor_to_temp()

static struct s_runtime Runtime;

struct s_runtime * get_runtime(void)
{
	return (&Runtime);
}

/**
 * Exponentially weighted moving average implementing a trivial LP filter
 http://www.rowetel.com/?p=1245
 https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 http://www.edn.com/design/systems-design/4320010/A-simple-software-lowpass-filter-suits-embedded-system-applications
 XXX if dt is 0 then the value will never be updated (dt has a 1s resolution)
 */
static temp_t temp_expw_mavg(const temp_t filtered, const temp_t new_sample, const time_t tau, const time_t dt)
{
	float alpha = (float)dt / (tau+dt);	// dt sampling itvl, tau = constante de temps

	/* dbgmsg("%d - (%f * (%d - %d)) = %f, %d", filtered, alpha, filtered, new_sample,
	       (filtered - (alpha * (filtered - new_sample))),
	       (temp_t)((filtered - roundf(alpha * (filtered - new_sample))))); */

	return (filtered - roundf(alpha * (filtered - new_sample)));
}

/**
 * Process raw sensor data and extract temperature values into the runtime temps[] array.
 * Applies a short-window LP filter on raw data to smooth out noise.
 */
static void parse_temps(void)
{
	static time_t lasttime = 0;	// in temp_expw_mavg, this makes alpha ~ 1, so the return value will be (prev value - 1*(0)) == prev value. Good
	const time_t dt = time(NULL) - lasttime;
	uint_fast8_t i;
	temp_t previous, current;

	for (i = 0; i<Runtime.config->nsensors; i++) {
		current = sensor_to_temp(Runtime.rWCHC_sensors[i]);
		previous = Runtime.temps[i];

		// apply LP filter with 5s time constant
		Runtime.temps[i] = temp_expw_mavg(previous, current, 5, dt);
	}

	lasttime = time(NULL);
}

/**
 * Process outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * t_filtered is t_outdoor filtered by the building time constant
 * @note must run at (ideally) fixed intervals
 * #warning no "parameter" check
 */
static void outdoor_temp()
{
	static time_t lasttime = 0;	// in temp_expw_mavg, this makes alpha ~ 1, so the return value will be (prev value - 1*(0)) == prev value. Good
	const time_t dt = time(NULL) - lasttime;
	static temp_t t_filtered = 0;	// outdoor temp filtered by building_tau

	Runtime.t_outdoor = get_temp(Runtime.config->id_temp_outdoor) + Runtime.config->set_temp_outdoor_offset;	// XXX checks

	// XXX REVISIT prevent running averages at less than building_tau/60 interval, otherwise the precision rounding error in temp_expw_mavg becomes too large
	if (dt < (Runtime.config->building_tau / 60))
		return;

	lasttime = time(NULL);

	t_filtered = temp_expw_mavg(t_filtered, Runtime.t_outdoor, Runtime.config->building_tau, dt);
	Runtime.t_outdoor_mixed = (Runtime.t_outdoor + t_filtered)/2;	// other possible calculation: 75% of t_outdoor + 25% of t_filtered - 211p15
	Runtime.t_outdoor_attenuated = temp_expw_mavg(Runtime.t_outdoor_attenuated, t_filtered, Runtime.config->building_tau, dt);
}


/**
 * Conditions for summer switch
 * summer mode is set on in ALL of the following conditions are met:
 * - t_outdoor > limit_tsummer
 * - t_outdoor_mixed > limit_tsummer
 * - t_outdoor_attenuated > limit_tsummer
 * summer mode is back off if ALL of the following conditions are met:
 * - t_outdoor < limit_tsummer
 * - t_outdoor_mixed < limit_tsummer
 * - t_outdoor_attenuated < limit_tsummer
 * State is preserved in all other cases
 * @note because we use AND, there's no need for histeresis
 */
static void runtime_summer(void)
{
	if ((Runtime.t_outdoor > Runtime.config->limit_tsummer)		&&
	    (Runtime.t_outdoor_mixed > Runtime.config->limit_tsummer)	&&
	    (Runtime.t_outdoor_attenuated > Runtime.config->limit_tsummer)) {
		Runtime.summer = true;
	}
	else {
		if ((Runtime.t_outdoor < Runtime.config->limit_tsummer)		&&
		    (Runtime.t_outdoor_mixed < Runtime.config->limit_tsummer)	&&
		    (Runtime.t_outdoor_attenuated < Runtime.config->limit_tsummer))
			Runtime.summer = false;
	}
}

void runtime_init(void)
{
	// fill the structure with zeroes, which turns everything off and sets sane values
	memset(&Runtime, 0x0, sizeof(Runtime));
}

/**
 * Set the global system operation mode.
 * @param sysmode desired operation mode.
 * @return error status
 */
int runtime_set_systemmode(const enum e_systemmode sysmode)
{
	switch (sysmode) {
		case SYS_OFF:
			Runtime.runmode = RM_OFF;
			Runtime.dhwmode = RM_OFF;
			break;
		case SYS_COMFORT:
			Runtime.runmode = RM_COMFORT;
			Runtime.dhwmode = RM_COMFORT;
			break;
		case SYS_ECO:
			Runtime.runmode = RM_ECO;
			Runtime.dhwmode = RM_ECO;
			break;
		case SYS_AUTO:		// XXX by default AUTO switches to frostfree until further settings
		case SYS_FROSTFREE:
			Runtime.runmode = RM_FROSTFREE;
			Runtime.dhwmode = RM_FROSTFREE;
			break;
		case SYS_MANUAL:
			Runtime.runmode = RM_MANUAL;
			Runtime.dhwmode = RM_MANUAL;
			break;
		case SYS_DHWONLY:
			Runtime.runmode = RM_FROSTFREE;
			Runtime.dhwmode = RM_COMFORT;	// XXX by default in comfort mode until further settings
			break;
		default:
			return (-EINVALID);
	}
	
	dbgmsg("sysmode: %d, runmode: %d, dhwmode: %d", sysmode, Runtime.runmode, Runtime.dhwmode);

	Runtime.systemmode = sysmode;

	return (ALL_OK);
}

/**
 * Set the global running mode.
 * @note This function is only valid to call when the global system mode is SYS_AUTO.
 * @param runmode desired operation mode, cannot be RM_AUTO.
 * @return error status
 */
int runtime_set_runmode(const enum e_runmode runmode)
{
	// runmode can only be directly modified in SYS_AUTO
	if (SYS_AUTO != Runtime.systemmode)
		return (-EINVALID);

	// if set, runmode cannot be RM_AUTO
	if (RM_AUTO == runmode)
		return (-EINVALIDMODE);

	Runtime.runmode = runmode;

	return (ALL_OK);
}

/**
 * Set the global dhw mode.
 * @note This function is only valid to call when the global system mode is SYS_AUTO or SYS_DHWONLY.
 * @param runmode desired operation mode, cannot be RM_AUTO or RM_DHWONLY.
 * @return error status
 */
int runtime_set_dhwmode(const enum e_runmode dhwmode)
{
	// runmode can only be directly modified in SYS_AUTO
	if ((SYS_AUTO != Runtime.systemmode) || (SYS_DHWONLY != Runtime.systemmode))
		return (-EINVALID);

	// if set, dhwmode cannot be RM_AUTO or RM_DHWONLY
	if ((RM_AUTO == dhwmode) || (RM_DHWONLY == dhwmode))
		return (-EINVALIDMODE);

	Runtime.dhwmode = dhwmode;

	return (ALL_OK);
}

/**
 * Prepare runtime for run loop.
 * Parse sensors and bring the plant online
 * @return exec status
 */
int runtime_online(void)
{
	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	parse_temps();

	outdoor_temp();
	// set init state of outdoor temperatures - XXX REVISIT
	if (0 == Runtime.t_outdoor_attenuated)
		Runtime.t_outdoor = Runtime.t_outdoor_mixed = Runtime.t_outdoor_attenuated = get_temp(Runtime.config->id_temp_outdoor);

	return (plant_online(Runtime.plant));
}

/**
 * Runtime run loop
 * @return exec status
 */
int runtime_run(void)
{
	static int count = 0;
	static tempid_t tempid = 1;
	enum e_systemmode cursysmode;
	int ret;

	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	// process data

	dbgmsg("begin.\tt_outdoor: %.1f, t_outmixed: %.1f, t_outatt: %.1f",
		temp_to_celsius(Runtime.t_outdoor), temp_to_celsius(Runtime.t_outdoor_mixed), temp_to_celsius(Runtime.t_outdoor_attenuated));
	
	parse_temps();

	if (Runtime.rWCHC_peripherals.LED2) {
		// clear alarm
		Runtime.rWCHC_peripherals.LED2 = 0;
		Runtime.rWCHC_peripherals.buzzer = 0;
		Runtime.rWCHC_peripherals.LCDbl = 0;
		lcd_update(true);
		// XXX reset runtime?
	}

	if (Runtime.rWCHC_peripherals.RQSW1) {
		// change system mode
		cursysmode = Runtime.systemmode;
		cursysmode++;
		Runtime.rWCHC_peripherals.RQSW1 = 0;
		count = 5;

		if (cursysmode > SYS_MANUAL)	// XXX last mode
			cursysmode = SYS_OFF;

		runtime_set_systemmode(cursysmode);	// XXX should only be active after timeout?
	}

	if (Runtime.rWCHC_peripherals.RQSW2) {
		// increase displayed tempid
		tempid++;
		Runtime.rWCHC_peripherals.RQSW2 = 0;
		count = 5;

		if (tempid > Runtime.config->nsensors)
			tempid = 1;
	}

	if (count) {
		Runtime.rWCHC_peripherals.LCDbl = 1;
		count--;
		if (!count)
			lcd_fade();
	}
	else
		Runtime.rWCHC_peripherals.LCDbl = 0;

	lcd_line1(tempid);
	lcd_update(false);

	outdoor_temp();
	runtime_summer();

	ret = plant_run(Runtime.plant);
	if (ret)
		goto out;

out:
	printf("\n");	// XXX
	return (ret);
}
