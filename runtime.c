//
//  runtime.c
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

#include "lib.h"
#include "plant.h"
#include "config.h"
#include "runtime.h"
#include "storage.h"
#include "timer.h"
#include "models.h"
#include "alarms.h"	// alarms_raise()
#include "hardware.h"	// for hardware_sensor_clone_temp()

#define LOG_INTVL_RUNTIME	900	///< log current runtime every X seconds

static const storage_version_t Runtime_sversion = 5;
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
			return (-EMISMATCH);
		
		Runtime.systemmode = temp_runtime.systemmode;
		Runtime.runmode = temp_runtime.runmode;
		Runtime.dhwmode = temp_runtime.dhwmode;
		pr_log(_("Runtime state restored"));
	}
	
	return (ALL_OK);
}

/**
 * Log key runtime variables.
 * @return exec status
 * @warning Locks runtime: do not call from master_thread
 */
static int runtime_async_log(void)
{
	const storage_version_t version = 3;
	static storage_keys_t keys[] = {
		"systemmode",
		"runmode",
		"dhwmode",
		"summer",
		"frost",
		"plant_sleep",
		"t_outdoor_60",
		"plant_hrequest",
	};
	static storage_values_t values[ARRAY_SIZE(keys)];
	unsigned int i = 0;
	
	pthread_rwlock_rdlock(&Runtime.runtime_rwlock);
	values[i++] = Runtime.systemmode;
	values[i++] = Runtime.runmode;
	values[i++] = Runtime.dhwmode;
	values[i++] = Runtime.summer;
	values[i++] = Runtime.frost;
	values[i++] = Runtime.plant_could_sleep;
	values[i++] = Runtime.t_outdoor_60;
	values[i++] = Runtime.plant_hrequest;
	pthread_rwlock_unlock(&Runtime.runtime_rwlock);
	
	assert(ARRAY_SIZE(keys) >= i);
	
	return (storage_log("log_runtime", &version, keys, values, i));
}

/**
 * Process outdoor temperature.
 * Computes outdoor temperature and "smoothed" outdoor temperature, with a safety
 * fallback in case of sensor failure.
 * @note must run at (ideally fixed) intervals >= 1s
 * @note this is part of the synchronous code path because moving it to a separate
 * thread would add overhead (locking) for essentially no performance improvement.
 */
static void outdoor_temp()
{
	const time_t last = Runtime.outdoor_time;	// previous sensor time. At first run: 0 which makes mavg return new sample
	time_t dt;
	temp_t toutdoor;
	int ret;

	ret = hardware_sensor_clone_temp(Runtime.config->id_temp_outdoor, &toutdoor);
	if (ALL_OK == ret) {
		hardware_sensor_clone_time(Runtime.config->id_temp_outdoor, &Runtime.outdoor_time);
		dt = Runtime.outdoor_time - last;
		Runtime.t_outdoor = toutdoor;
		Runtime.t_outdoor_60 = temp_expw_mavg(Runtime.t_outdoor_60, Runtime.t_outdoor, 60, dt);
	}
	else {
		// in case of outdoor sensor failure, assume outdoor temp is tfrost-1: ensures frost protection
		Runtime.t_outdoor_60 = Runtime.t_outdoor = Runtime.config->limit_tfrost-1;
		alarms_raise(ret, _("Outdoor sensor failure"), _("Outdr sens fail"));
	}
}


/**
 * Conditions for summer switch.
 * If ALL bmodels are compatible with summer mode, summer mode is set.
 * If ANY bmodel is incompatible with summer mode, summer mode is unset.
 * Lockless by design.
 */
static void runtime_summer(void)
{
	struct s_bmodel_l * bmodelelmt;
	bool summer = true;

	if (!Runtime.config->limit_tsummer)
		return;	// invalid limit, don't do anything

	for (bmodelelmt = Runtime.models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		summer &= bmodelelmt->bmodel->run.summer;

	Runtime.summer = summer;
}

/**
 * Conditions for frost switch.
 * Trigger frost protection flag when t_outdoor_60 < limit_tfrost.
 * @note there is a fixed 1K positive hysteresis (on untrip)
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
 * @note SYS_AUTO does not change the current runmode and dhwmode.
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
		case SYS_AUTO:		// NOTE by default AUTO does not change the current run/dhwmodes
			break;
		case SYS_FROSTFREE:
			Runtime.runmode = RM_FROSTFREE;
			Runtime.dhwmode = RM_FROSTFREE;
			break;
		case SYS_TEST:
			Runtime.runmode = RM_TEST;
			Runtime.dhwmode = RM_TEST;
			break;
		case SYS_DHWONLY:
			Runtime.runmode = RM_DHWONLY;
			Runtime.dhwmode = RM_COMFORT;	// NOTE by default in comfort mode until further settings
			break;
		case SYS_UNKNOWN:
		default:
			return (-EINVALID);
	}
	
	dbgmsg("sysmode: %d, runmode: %d, dhwmode: %d", sysmode, Runtime.runmode, Runtime.dhwmode);
	Runtime.systemmode = sysmode;
	
	runtime_save();

	pr_log(_("System mode set: %d"), sysmode);

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
	switch (runmode) {
		case RM_OFF:
		case RM_COMFORT:
		case RM_ECO:
		case RM_FROSTFREE:
		case RM_DHWONLY:
		case RM_TEST:
			break;
		case RM_AUTO:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	Runtime.runmode = runmode;

	runtime_save();

	pr_log(_("Run mode set: %d"), runmode);

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
	switch (dhwmode) {
		case RM_OFF:
		case RM_COMFORT:
		case RM_ECO:
		case RM_FROSTFREE:
		case RM_TEST:
			break;
		case RM_AUTO:
		case RM_DHWONLY:
		case RM_UNKNOWN:
		default:
			return (-EINVALIDMODE);
	}

	Runtime.dhwmode = dhwmode;

	runtime_save();

	pr_log(_("DHW mode set: %d"), dhwmode);

	return (ALL_OK);
}

/**
 * Prepare runtime for run loop.
 * Parse sensors and bring the plant online
 * @return exec status
 */
int runtime_online(void)
{
	int ret;

	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant || !Runtime.models)
		return (-ENOTCONFIGURED);
	
	Runtime.start_time = time(NULL);

	// make sure specified outdoor sensor is available
	ret = hardware_sensor_clone_time(Runtime.config->id_temp_outdoor, NULL);
	if (ALL_OK != ret)
		return (ret);

	runtime_restore();

	outdoor_temp();

	timer_add_cb(LOG_INTVL_RUNTIME, runtime_async_log);

	models_online(Runtime.models);

	return (plant_online(Runtime.plant));
}

/**
 * Runtime run loop
 * @return exec status
 */
int runtime_run(void)
{
	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant || !Runtime.models)
		return (-ENOTCONFIGURED);

	// process data

	dbgmsg("t_outdoor: %.1f, t_60: %.1f",
	       temp_to_celsius(Runtime.t_outdoor), temp_to_celsius(Runtime.t_outdoor_60));

	outdoor_temp();
	runtime_frost();

	models_run(Runtime.models);

	runtime_summer();

	return (plant_run(Runtime.plant));
}

/**
 * Offline runtime.
 * @return exec status
 */
int runtime_offline(void)
{
	if (!Runtime.config || !Runtime.config->configured || !Runtime.plant || !Runtime.models)
		return (-ENOTCONFIGURED);

	if (runtime_save() != ALL_OK)
		dbgerr("runtime save failed");

	if (plant_offline(Runtime.plant) != ALL_OK)
		dbgerr("plant offline failed");

	if (models_offline(Runtime.models) != ALL_OK)
		dbgerr("models offline failed");

	return (ALL_OK);
}

/**
 * Exit runtime.
 */
void runtime_exit(void)
{
	runtime_init();		// clear runtime
}
