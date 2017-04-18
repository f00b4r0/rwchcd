//
//  rwchcd_runtime.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Runtime implementation.
 */

#include <time.h>	// time_t
#include <string.h>	// memset/memcpy
#include <assert.h>

#include "rwchcd_lib.h"
#include "rwchcd_plant.h"
#include "rwchcd_runtime.h"
#include "rwchcd_storage.h"
#include "rwchcd_timer.h"

#define OUTDOOR_AVG_UPDATE_DT	600	///< prevents running averages at less than 10mn interval. Should be good up to 100h building_tau.
#define LOG_INTVL_RUNTIME	OUTDOOR_AVG_UPDATE_DT
#define LOG_INTVL_TEMPS		60

static const storage_version_t Runtime_sversion = 4;
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
 * Log key runtime variables.
 * @return exec status
 * @warning Locks runtime: do not call from master_thread
 */
static int runtime_async_log(void)
{
	const storage_version_t version = 2;
	static storage_keys_t keys[] = {
		"systemmode",
		"runmode",
		"dhwmode",
		"summer",
		"frost",
		"t_outdoor_60",
		"t_outdoor_filtered",
		"t_outdoor_attenuated",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;
	
	pthread_rwlock_rdlock(&Runtime.runtime_rwlock);
	values[i++] = Runtime.systemmode;
	values[i++] = Runtime.runmode;
	values[i++] = Runtime.dhwmode;
	values[i++] = Runtime.summer;
	values[i++] = Runtime.frost;
	values[i++] = Runtime.t_outdoor_60;
	values[i++] = Runtime.t_outdoor_filtered;
	values[i++] = Runtime.t_outdoor_attenuated;
	pthread_rwlock_unlock(&Runtime.runtime_rwlock);
	
	assert(ARRAY_SIZE(keys) >= i);
	
	return (storage_log("log_runtime", &version, keys, values, i));
}

/**
 * Log internal temperatures.
 * @return exec status
 * @warning Locks runtime: do not call from master_thread
 */
static int runtime_async_log_temps(void)
{
	const storage_version_t version = 2;
	static storage_keys_t keys[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	int i = 0;
	
	assert(ARRAY_SIZE(keys) >= RWCHCD_NTEMPS);
	
	pthread_rwlock_rdlock(&Runtime.runtime_rwlock);
	for (i = 0; i < Runtime.config->nsensors; i++)
		values[i] = Runtime.temps[i];
	pthread_rwlock_unlock(&Runtime.runtime_rwlock);
	
	return (storage_log("log_temps", &version, keys, values, i));
}

/**
 * Process outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * This function is designed so that at init time, when the variables are all 0,
 * the averages will take the value of the current outdoor temperature.
 * @note must run at (ideally) fixed intervals
 * @note this is part of the synchronous code path because moving it to a separate
 * thread would add overhead (locking) for essentially no performance improvement.
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

	last60 = now;
	
	if (dt >= OUTDOOR_AVG_UPDATE_DT) {
		Runtime.t_outdoor_ltime = now;

		Runtime.t_outdoor_filtered = temp_expw_mavg(Runtime.t_outdoor_filtered, Runtime.t_outdoor_60, Runtime.config->building_tau, dt);
		Runtime.t_outdoor_attenuated = temp_expw_mavg(Runtime.t_outdoor_attenuated, Runtime.t_outdoor_filtered, Runtime.config->building_tau, dt);
		
		runtime_save();
	}

	// calculate mixed temp last: makes it work at init
	Runtime.t_outdoor_mixed = (Runtime.t_outdoor_60 + Runtime.t_outdoor_filtered)/2;	// XXX other possible calculation: X% of t_outdoor + 1-X% of t_filtered. Current setup is 50%
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
 * @note there is a fixed 1K positive histeresis (on untrip)
 */
static void runtime_frost(void)
{
	if (!Runtime.config->limit_tfrost)
		return;	// invalid limit, don't do anything
	
	if ((Runtime.t_outdoor_60 < Runtime.config->limit_tfrost))
		Runtime.frost = true;
	else if ((Runtime.t_outdoor_60 > (Runtime.config->limit_tfrost + deltaK_to_temp(1))))
		Runtime.frost = false;
}

/**
 * Init runtime
 */
int runtime_init(void)
{
	// fill the structure with zeroes, which turns everything off and sets sane values
	memset(&Runtime, 0x0, sizeof(Runtime));
	
	return (pthread_rwlock_init(&Runtime.runtime_rwlock, NULL));
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

	pr_log("system mode set: %d", sysmode);

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
	if ((RM_AUTO == runmode) || (runmode >= RM_UNKNOWN))
		return (-EINVALIDMODE);

	Runtime.runmode = runmode;

	runtime_save();

	pr_log("run mode set: %d", runmode);

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
	// dhwmode can only be directly modified in SYS_AUTO or SYS_DHWONLY
	if (!((SYS_AUTO == Runtime.systemmode) || (SYS_DHWONLY == Runtime.systemmode)))
		return (-EINVALID);

	// if set, dhwmode cannot be RM_AUTO or RM_DHWONLY
	if ((RM_AUTO == dhwmode) || (RM_DHWONLY == dhwmode) || (dhwmode >= RM_UNKNOWN))
		return (-EINVALIDMODE);

	Runtime.dhwmode = dhwmode;

	runtime_save();

	pr_log("DHW mode set: %d", dhwmode);

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

	timer_add_cb(LOG_INTVL_RUNTIME, runtime_async_log);
	timer_add_cb(LOG_INTVL_TEMPS, runtime_async_log_temps);
	
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

/**
 * Exit runtime.
 */
void runtime_exit(void)
{
	runtime_init();		// clear runtime
}
