//
//  rwchcd_runtime.c
//  rwchcd
//
//  Created by Thibaut VARENE on 13/09/16.
//
//

#include <time.h>	// time_t
#include "rwchcd_hardware.h"
#include "rwchcd_runtime.h"


static struct s_runtime Runtime;

struct s_runtime * get_runtime(void)
{
	return (&Runtime);
}

/**
 * get temp from a given temp id
 * @return temp if id valid, 0 otherwise
 */
temp_t get_temp(const tempid_t id)
{
	const struct s_runtime * const runtime = get_runtime();

	if (id > runtime->config->nsensors)
		return (0);

	return (runtime->temps[id]);	// XXX REVISIT lock
}

static void parse_temps(void)
{
	struct s_runtime * const runtime = get_runtime();
	int i;

	for (i = 0; i<runtime->config->nsensors; i++) {
		runtime->temps[i] = sensor_to_temp(runtime->rWCHC_sensors[i]);
	}
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
	struct s_runtime * const runtime = get_runtime();
	const time_t dt = time(NULL) - lasttime;
	lasttime = time(NULL);

	runtime->t_outdoor = get_temp(runtime->config->id_temp_outdoor);	// XXX checks
	runtime->t_outdoor_mixed = expw_mavg(runtime->t_outdoor_mixed, runtime->t_outdoor, runtime->config->building_tau, dt);
	runtime->t_outdoor_attenuated = expw_mavg(runtime->t_outdoor_attenuated, runtime->t_outdoor_mixed, runtime->config->building_tau, dt);
}


static int init_process()
{
	struct s_runtime * const runtime = get_runtime();

	runtime->t_outdoor = runtime->t_outdoor_mixed = runtime->t_outdoor_attenuated = get_temp(runtime->config->id_temp_outdoor);

	// set mixing valve to known start state
	//set_mixer_pos(&Valve, -1);	// force fully closed during more than normal ete_time

}
