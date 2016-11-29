//
//  rwchcd_runtime.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Runtime implementation.
 */

#include <time.h>	// time_t
#include <string.h>	// memset/memcpy
#include "rwchcd_lib.h"
#include "rwchcd_plant.h"
#include "rwchcd_runtime.h"
#include "rwchcd_storage.h"

static const storage_version_t Runtime_sversion = 3;
static struct s_runtime Runtime;

/**
 * Get current program runtime
 * @return pointer to current runtime.
 */
struct s_runtime * get_runtime(void)
{
	return (&Runtime);
}

/**
 * Save runtime to permanent storage
 * @return exec status
 */
static int runtime_save(void)
{
	return (storage_dump("runtime", &Runtime_sversion, &Runtime, sizeof(Runtime)));
}

/**
 * Restore runtime from permanent storage
 * @return exec status
 */
static int runtime_restore(void)
{
	struct s_runtime temp_runtime;
	storage_version_t sversion;
	
	// try to restore key elements of last runtime
	if (storage_fetch("runtime", &sversion, &temp_runtime, sizeof(temp_runtime)) == ALL_OK) {
		if (Runtime_sversion != sversion)
			return (ALL_OK);	// XXX
		
		Runtime.t_outdoor_ltime = temp_runtime.t_outdoor_ltime;
		Runtime.t_outdoor_filtered = temp_runtime.t_outdoor_filtered;
		Runtime.t_outdoor_attenuated = temp_runtime.t_outdoor_attenuated;
		Runtime.systemmode = temp_runtime.systemmode;
		Runtime.runmode = temp_runtime.runmode;
		Runtime.dhwmode = temp_runtime.dhwmode;
	}
	else
		dbgmsg("storage_fetch failed");
	
	return (ALL_OK);
}

/**
 * Reset runtime outdoor temperatures to sane values
 */
static inline void outdoor_temp_reset(void)
{
	// set init state of outdoor temperatures - XXX REVISIT
	if (0 == Runtime.t_outdoor_attenuated)
		Runtime.t_outdoor = Runtime.t_outdoor_60 = Runtime.t_outdoor_filtered = Runtime.t_outdoor_mixed = Runtime.t_outdoor_attenuated = get_temp(Runtime.config->id_temp_outdoor);

}

/**
 * Process outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * @note must run at (ideally) fixed intervals
 * #warning no "parameter" check
 */
static void outdoor_temp()
{
	static time_t last60 = 0;	// in temp_expw_mavg, this makes alpha ~ 1, so the return value will be (prev value - 1*(0)) == prev value. Good
	const time_t now = time(NULL);
	const time_t dt = now - Runtime.t_outdoor_ltime;
	const time_t dt60 = now - last60;
	const temp_t toutdoor = get_temp(Runtime.config->id_temp_outdoor);
	
	if (validate_temp(toutdoor) != ALL_OK)
		Runtime.t_outdoor = Runtime.config->limit_tfrost-1;	// in case of outdoor sensor failure, assume outdoor temp is tfrost-1: ensures frost protection
	else
		Runtime.t_outdoor = toutdoor + Runtime.config->set_temp_outdoor_offset;
	
	Runtime.t_outdoor_60 = temp_expw_mavg(Runtime.t_outdoor_60, Runtime.t_outdoor, 60, dt60);
	Runtime.t_outdoor_mixed = (Runtime.t_outdoor_60 + Runtime.t_outdoor_filtered)/2;	// XXX other possible calculation: X% of t_outdoor + 1-X% of t_filtered. Current setup is 50%

	last60 = now;
	
	// XXX REVISIT prevent running averages at less than building_tau/60 interval, otherwise the precision rounding error in temp_expw_mavg becomes too large
	if (dt < (Runtime.config->building_tau / 60))
		return;

	Runtime.t_outdoor_ltime = now;

	Runtime.t_outdoor_filtered = temp_expw_mavg(Runtime.t_outdoor_filtered, Runtime.t_outdoor, Runtime.config->building_tau, dt);
	Runtime.t_outdoor_attenuated = temp_expw_mavg(Runtime.t_outdoor_attenuated, Runtime.t_outdoor_filtered, Runtime.config->building_tau, dt);
	
	runtime_save();
}


/**
 * Conditions for summer switch.
 * summer mode is set on in ALL of the following conditions are met:
 * - t_outdoor_60 > limit_tsummer
 * - t_outdoor_mixed > limit_tsummer
 * - t_outdoor_attenuated > limit_tsummer
 * summer mode is back off if ALL of the following conditions are met:
 * - t_outdoor_60 < limit_tsummer
 * - t_outdoor_mixed < limit_tsummer
 * - t_outdoor_attenuated < limit_tsummer
 * State is preserved in all other cases
 * @note because we use AND, there's no need for histeresis
 */
static void runtime_summer(void)
{
	if (!Runtime.config->limit_tsummer)
		return;	// invalid limit, don't do anything
	
	if ((Runtime.t_outdoor_60 > Runtime.config->limit_tsummer)	&&
	    (Runtime.t_outdoor_mixed > Runtime.config->limit_tsummer)	&&
	    (Runtime.t_outdoor_attenuated > Runtime.config->limit_tsummer)) {
		Runtime.summer = true;
	}
	else {
		if ((Runtime.t_outdoor_60 < Runtime.config->limit_tsummer)	&&
		    (Runtime.t_outdoor_mixed < Runtime.config->limit_tsummer)	&&
		    (Runtime.t_outdoor_attenuated < Runtime.config->limit_tsummer))
			Runtime.summer = false;
	}
}

/**
 * Conditions for frost switch.
 * Trigger frost protection flag when t_outdoor_60 < limit_tfrost.
 * @note there is no histeresis
 */
static void runtime_frost(void)
{
	if (!Runtime.config->limit_tfrost)
		return;	// invalid limit, don't do anything
	
	if ((Runtime.t_outdoor_60 < Runtime.config->limit_tfrost))
		Runtime.frost = true;
	else
		Runtime.frost = false;
}

/**
 * Init runtime
 */
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
			Runtime.runmode = RM_DHWONLY;
			Runtime.dhwmode = RM_COMFORT;	// XXX by default in comfort mode until further settings
			break;
		case SYS_UNKNOWN:
		default:
			return (-EINVALID);
	}
	
	dbgmsg("sysmode: %d, runmode: %d, dhwmode: %d", sysmode, Runtime.runmode, Runtime.dhwmode);

	Runtime.systemmode = sysmode;
	
	runtime_save();

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

	runtime_save();
	
	return (ALL_OK);
}

/**
 * Set the global dhw mode.
 * @note This function is only valid to call when the global system mode is SYS_AUTO or SYS_DHWONLY.
 * @param dhwmode desired operation mode, cannot be RM_AUTO or RM_DHWONLY.
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

	runtime_save();
	
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
	
	Runtime.start_time = time(NULL);
	
	runtime_restore();

	outdoor_temp();
	outdoor_temp_reset();

	return (plant_online(Runtime.plant));
}

/**
 * Runtime run loop
 * @return exec status
 */
int runtime_run(void)
{
	int ret;

	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	// process data

	dbgmsg("t_outdoor: %.1f, t_60: %.1f, t_filt: %.1f, t_outmixed: %.1f, t_outatt: %.1f",
	       temp_to_celsius(Runtime.t_outdoor), temp_to_celsius(Runtime.t_outdoor_60), temp_to_celsius(Runtime.t_outdoor_filtered),
	       temp_to_celsius(Runtime.t_outdoor_mixed), temp_to_celsius(Runtime.t_outdoor_attenuated));

	outdoor_temp();
	runtime_summer();
	runtime_frost();
	
	ret = plant_run(Runtime.plant);
	if (ret)
		goto out;

out:
	return (ret);
}

/**
 * Offline runtime.
 * @return exec status
 */
int runtime_offline(void)
{
	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	runtime_save();
	
	return (plant_offline(Runtime.plant));
}

void runtime_exit(void)
{
	runtime_init();		// clear runtime
}
