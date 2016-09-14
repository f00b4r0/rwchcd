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

static void outdoor_temp()
{
	static time_t lasttime = time(NULL);
	const time_t dt = time(NULL) - lasttime;
	lasttime = time(NULL);

	Runtime.t_outdoor = get_temp(Runtime.config->id_temp_outdoor);	// XXX checks
	Runtime.t_outdoor_mixed = expw_mavg(Runtime.t_outdoor_mixed, Runtime.t_outdoor, Runtime.config->building_tau, dt);
	Runtime.t_outdoor_attenuated = expw_mavg(Runtime.t_outdoor_attenuated, Runtime.t_outdoor_mixed, Runtime.config->building_tau, dt);
}

void runtime_init(void)
{
	// fill the structure with zeroes, which turns everything off and sets sane values
	memset(&Runtime, 0x0, sizeof(Runtime));
}

int runtime_set_systemmode(enum e_systemmode sysmode)
{
	switch (sysmode) {
		case SYS_OFF:
			Runtime.set_runmode = RM_OFF;
			Runtime.dhwmode = RM_OFF;
			break;
		case SYS_COMFORT:
			Runtime.set_runmode = RM_COMFORT;
			Runtime.dhwmode = RM_COMFORT;
			break;
		case SYS_ECO:
			Runtime.set_runmode = RM_ECO;
			Runtime.dhwmode = RM_ECO;
			break;
		case SYS_FROSTFREE:
			Runtime.set_runmode = RM_FROSTFREE;
			Runtime.dhwmode = RM_FROSTFREE;
			break;
		case SYS_MANUAL:
			Runtime.set_runmode = RM_MANUAL;
			Runtime.dhwmode = RM_MANUAL;
			break;
		case SYS_DHWONLY:
			Runtime.set_runmode = RM_FROSTFREE;
			Runtime.dhwmode = RM_COMFORT;	// XXX by default in comfort mode
			break;
		default:
			break;	// XXX AUTO is handled differently
	}

	Runtime.systemmode = sysmode;

	return (ALL_OK);
}

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

int runtime_set_dhwmode(enum e_runmode dhwmode)
{
	// runmode can only be directly modified in SYS_AUTO
	if (SYS_AUTO != Runtime.systemmode)
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
	int ret = ALL_OK, i;

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

	ret = plant_run(Runtime.plant);
	if (ret)
		goto out;

	// send SPI data
	ret = hardware_relays_write(&(Runtime.rWCHC_relays));
	if (ret)
		return (-ESPI);

	ret = hardware_periphs_write(&(Runtime.rWCHC_peripherals));
	if (ret)
		return (-ESPI);

out:
	return (ret);
}
