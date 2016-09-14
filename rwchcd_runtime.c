//
//  rwchcd_runtime.c
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#include <time.h>	// time_t
#include <string.h>	// memset/memcpy
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_runtime.h"


static struct s_runtime Runtime;

struct s_runtime * get_runtime(void)
{
	return (&Runtime);
}

static inline void parse_temps(void)
{
	int i;

	for (i = 0; i<Runtime.config->nsensors; i++)
		Runtime.temps[i] = sensor_to_temp(Runtime.rWCHC_sensors[i]);
}

/**
 * Exponentially weighted moving average implementing a trivial LP filter
 http://www.rowetel.com/blog/?p=1245
 https://kiritchatterjee.wordpress.com/2014/11/10/a-simple-digital-low-pass-filter-in-c/
 */
static float expw_mavg(temp_t filtered, temp_t new_sample, time_t tau, time_t dt)
{
	float alpha = dt / (tau+dt);	// dt sampling itvl, tau = constante de temps

	return (filtered - (alpha * (filtered - new_sample)));
}

/**
 * Process outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * @note must run at (ideally) fixed intervals
 */
static void outdoor_temp()
{
	static time_t lasttime = 0;	// in expw_mavg, this makes alpha ~ 1, so the return value will be (prev value - 1*(0)) == prev value. Good
	const time_t dt = time(NULL) - lasttime;
	lasttime = time(NULL);

	Runtime.t_outdoor = get_temp(Runtime.config->id_temp_outdoor);	// XXX checks
	Runtime.t_outdoor_mixed = expw_mavg(Runtime.t_outdoor_mixed, Runtime.t_outdoor, Runtime.config->building_tau, dt);
	Runtime.t_outdoor_attenuated = expw_mavg(Runtime.t_outdoor_attenuated, Runtime.t_outdoor_mixed, Runtime.config->building_tau, dt);
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
int runtime_set_systemmode(enum e_systemmode sysmode)
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

	Runtime.systemmode = sysmode;

	return (ALL_OK);
}

/**
 * Set the global running mode.
 * @note This function is only valid to call when the global system mode is SYS_AUTO.
 * @param runmode desired operation mode, cannot be RM_AUTO.
 * @return error status
 */
int runtime_set_runmode(enum e_runmode runmode)
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
int runtime_set_dhwmode(enum e_runmode dhwmode)
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

int runtime_run(void)
{
	static uint16_t rawsensors[RWCHC_NTSENSORS];
	int ret = ALL_OK;

	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	// fetch SPI data
	ret = hardware_sensors_read(rawsensors, Runtime.config->nsensors);
	if (ret)
		goto out;

	// copy valid data to runtime environment
	memcpy(Runtime.rWCHC_sensors, rawsensors, sizeof(Runtime.rWCHC_sensors));

	// set init state of outdoor temperatures - XXX REVISIT
	if (0 == Runtime.t_outdoor_attenuated)
		Runtime.t_outdoor = Runtime.t_outdoor_mixed = Runtime.t_outdoor_attenuated = get_temp(Runtime.config->id_temp_outdoor);

	// process data
	parse_temps();
	outdoor_temp();
	runtime_summer();

	ret = plant_run(Runtime.plant);
	if (ret)
		goto out;

	// send SPI data
	ret = hardware_rwchcrelays_write(&(Runtime.rWCHC_relays));
	if (ret)
		return (-ESPI);

	ret = hardware_rwchcperiphs_write(&(Runtime.rWCHC_peripherals));
	if (ret)
		return (-ESPI);

out:
	return (ret);
}
