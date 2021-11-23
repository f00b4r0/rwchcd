//
//  runtime.c
//  rwchcd
//
//  (C) 2016-2018,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Runtime implementation.
 * Controls system-wide plant operation.
 */

#include <string.h>	// memset/memcpy
#include <assert.h>
#include <stdatomic.h>

#include "plant/plant.h"
#include "runtime.h"
#include "storage.h"
#include "log/log.h"
#include "alarms.h"	// alarms_raise()
#include "timekeep.h"	// timekeep_usleep()

static atomic_flag Runtime_savef = ATOMIC_FLAG_INIT;

static int runtime_logdata_cb(struct s_log_data * const ldata, const void * const object);

static const storage_version_t Runtime_sversion = 10;

static struct s_runtime Runtime;	///< Runtime private data

static const log_key_t runtime_keys[] = {
	"systemmode",
	"runmode",
	"dhwmode",
};
static const enum e_log_metric runtime_metrics[] = {
	LOG_METRIC_IGAUGE,
	LOG_METRIC_IGAUGE,
	LOG_METRIC_IGAUGE,
};

/** Runtime log source */
static const struct s_log_source Runtime_lsrc = {
	.log_sched = LOG_SCHED_15mn,
	.basename = "runtime",
	.identifier = "master",
	.version = 7,
	.nkeys = ARRAY_SIZE(runtime_keys),
	.keys = runtime_keys,
	.metrics = runtime_metrics,
	.logdata_cb = runtime_logdata_cb,
	.object = NULL,
};

/**
 * Get current program runtime
 * @return pointer to current runtime.
 */
struct s_runtime * runtime_get(void)
{
	return (&Runtime);
}

/**
 * Save runtime to permanent storage
 * @return exec status
 */
static int runtime_save(void)
{
	return (storage_dump("runtime.state", &Runtime_sversion, &Runtime.run, sizeof(Runtime.run)));
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
	if (storage_fetch("runtime.state", &sversion, &temp_runtime.run, sizeof(temp_runtime.run)) == ALL_OK) {
		if (Runtime_sversion != sversion)
			return (-EMISMATCH);
		
		aser(&Runtime.run.systemmode, aler(&temp_runtime.run.systemmode));
		aser(&Runtime.run.runmode, aler(&temp_runtime.run.runmode));
		aser(&Runtime.run.dhwmode, aler(&temp_runtime.run.dhwmode));
		aser(&Runtime.run.stopdhw, aler(&temp_runtime.run.stopdhw));
		pr_log(_("Runtime state restored"));
	}
	
	return (ALL_OK);
}

/**
 * Runtime variable data log callback.
 * @param ldata the log data to populate
 * @param object unused
 * @return exec status
 * @warning Locks runtime: do not call from master_thread
 */
static int runtime_logdata_cb(struct s_log_data * const ldata, const void * const object __attribute__((unused)))
{
	unsigned int i = 0;

	assert(ldata);
	assert(ldata->nkeys >= ARRAY_SIZE(runtime_keys));
	
	ldata->values[i++].i = aler(&Runtime.run.systemmode);
	ldata->values[i++].i = aler(&Runtime.run.runmode);
	ldata->values[i++].i = aler(&Runtime.run.dhwmode);

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Init runtime
 */
int runtime_init(void)
{
	// fill the structure with zeroes, which turns everything off and sets sane values
	memset(&Runtime, 0x0, sizeof(Runtime));
	
	return (ALL_OK);
}

/**
 * Set the global system operation mode.
 * @note SYS_AUTO and SYS_MANUAL do not change the current runmode and dhwmode.
 * @param sysmode desired operation mode.
 * @return error status
 */
int runtime_set_systemmode(const enum e_systemmode sysmode)
{
	enum e_runmode runmode, dhwmode;

	while(unlikely(atomic_flag_test_and_set_explicit(&Runtime_savef, memory_order_acquire)))
		timekeep_usleep(500);	// yield

	switch (sysmode) {
		case SYS_OFF:
			runmode = RM_OFF;
			dhwmode = RM_OFF;
			break;
		case SYS_COMFORT:
			runmode = RM_COMFORT;
			dhwmode = RM_COMFORT;
			break;
		case SYS_ECO:
			runmode = RM_ECO;
			dhwmode = RM_ECO;
			break;
		case SYS_AUTO:		// NOTE by default AUTO does not change the current run/dhwmodes
		case SYS_MANUAL:
			goto out;
		case SYS_FROSTFREE:
			runmode = RM_FROSTFREE;
			dhwmode = RM_FROSTFREE;
			break;
		case SYS_TEST:
			runmode = RM_TEST;
			dhwmode = RM_TEST;
			break;
		case SYS_DHWONLY:
			runmode = RM_DHWONLY;
			dhwmode = RM_COMFORT;	// NOTE by default in comfort mode until further settings
			break;
		case SYS_NONE:
		case SYS_UNKNOWN:
		default:
			return (-EINVALID);
	}

	aser(&Runtime.run.runmode, runmode);
	aser(&Runtime.run.dhwmode, dhwmode);

	dbgmsg(1, 1, "sysmode: %d, runmode: %d, dhwmode: %d", sysmode, runmode, dhwmode);

out:
	aser(&Runtime.run.systemmode, sysmode);
	
	runtime_save();

	atomic_flag_clear_explicit(&Runtime_savef, memory_order_release);

	pr_log(_("System mode set: %d"), sysmode);

	return (ALL_OK);
}

/**
 * Set the global running mode.
 * @note This function is only valid to call when the global system mode is SYS_MANUAL or SYS_AUTO.
 * @param runmode desired operation mode, cannot be RM_AUTO.
 * @return error status
 */
int runtime_set_runmode(const enum e_runmode runmode)
{
	const enum e_systemmode sysmode = aler(&Runtime.run.systemmode);

	// runmode can only be directly modified in SYS_AUTO or SYS_MANUAL
	if (!((SYS_MANUAL == sysmode) || (SYS_AUTO == sysmode)))
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

	while(unlikely(atomic_flag_test_and_set_explicit(&Runtime_savef, memory_order_acquire)))
		timekeep_usleep(500);	// yield

	aser(&Runtime.run.runmode, runmode);

	runtime_save();

	atomic_flag_clear_explicit(&Runtime_savef, memory_order_release);

	pr_log(_("Run mode set: %d"), runmode);

	return (ALL_OK);
}

/**
 * Set the global dhw mode.
 * @note This function is only valid to call when the global system mode is SYS_MANUAL, SYS_AUTO or SYS_DHWONLY.
 * @param dhwmode desired operation mode, cannot be RM_AUTO or RM_DHWONLY.
 * @return error status
 */
int runtime_set_dhwmode(const enum e_runmode dhwmode)
{
	const enum e_systemmode sysmode = aler(&Runtime.run.systemmode);

	// dhwmode can only be directly modified in SYS_AUTO, SYS_MANUAL or SYS_DHWONLY
	if (!((SYS_MANUAL == sysmode) || (SYS_AUTO == sysmode) || (SYS_DHWONLY == sysmode)))
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

	while(unlikely(atomic_flag_test_and_set_explicit(&Runtime_savef, memory_order_acquire)))
		timekeep_usleep(500);	// yield

	aser(&Runtime.run.dhwmode, dhwmode);

	runtime_save();

	atomic_flag_clear_explicit(&Runtime_savef, memory_order_release);

	pr_log(_("DHW mode set: %d"), dhwmode);

	return (ALL_OK);
}

/**
 * Prepare runtime for run loop.
 * Bring plant online
 * @return exec status
 */
int runtime_online(void)
{
	if (!Runtime.set.configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	runtime_restore();

	log_register(&Runtime_lsrc);

	alarms_online(Runtime.set.notifier);

	return (plant_online(Runtime.plant));
}

/**
 * Runtime run loop
 * @return exec status
 */
int runtime_run(void)
{
	if (unlikely(!Runtime.set.configured || !Runtime.plant))
		return (-ENOTCONFIGURED);

	return (plant_run(Runtime.plant));
}

/**
 * Offline runtime.
 * @return exec status
 */
int runtime_offline(void)
{
	if (!Runtime.set.configured || !Runtime.plant)
		return (-ENOTCONFIGURED);

	if (runtime_save() != ALL_OK)
		pr_err(_("Failed to save runtime"));

	log_deregister(&Runtime_lsrc);

	if (plant_offline(Runtime.plant) != ALL_OK)
		pr_err(_("Failed to offline plant"));

	alarms_offline();	// depends on nothing

	return (ALL_OK);
}

/**
 * Exit runtime.
 */
void runtime_exit(void)
{
	plant_del(Runtime.plant);
	runtime_init();		// clear runtime
}

/**
 * Get the global system mode.
 * @return current systemmode
 */
enum e_systemmode runtime_systemmode(void)
{
	return (aler(&Runtime.run.systemmode));
}

/**
 * Get the global running mode.
 * @return current runmode
 */
enum e_runmode runtime_runmode(void)
{
	return (aler(&Runtime.run.runmode));
}

/**
 * Get the global dhw mode.
 * @return current dhwmode
 */
enum e_runmode runtime_dhwmode(void)
{
	return (aler(&Runtime.run.dhwmode));
}

/**
 * Get the global DHW kill switch state.
 * @return current state
 */
bool runtime_get_stopdhw(void)
{
	return (aler(&Runtime.run.stopdhw));
}

/**
 * Set the global DHW kill switch state.
 * @return exec status
 */
int runtime_set_stopdhw(bool state)
{
	aser(&Runtime.run.stopdhw, state);
	return (ALL_OK);
}
