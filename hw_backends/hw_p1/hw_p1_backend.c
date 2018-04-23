//
//  hw_p1_backend.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 backend implementation.
 *
 * @note while the backend interface makes use of the @b priv data pointer for
 * documentation purposes, behind the scenes the code assumes access to statically
 * allocated data.
 */

#include <assert.h>
#include <string.h>

#include "hw_backends.h"
#include "hw_p1.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1_setup.h"
#include "hw_p1_filecfg.h"
#include "runtime.h"
#include "alarms.h"
#include "timer.h"

#define INIT_MAX_TRIES		10	///< how many times hardware init should be retried
#define LOG_INTVL_TEMPS		60	///< log temperatures every X seconds

/**
 * Initialize hardware and ensure connection is set
 * @param priv private hardware data
 * @return error state
 */
__attribute__((warn_unused_result)) static int hw_p1_init(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	int ret, i = 0;

	if (!hw)
		return (-EINVALID);

	if (hw_p1_spi_init() < 0)
		return (-EINIT);

	// fetch firmware version
	do {
		ret = hw_p1_spi_fwversion();
	} while ((ret <= 0) && (i++ < INIT_MAX_TRIES));

	if (ret <= 0) {
		dbgerr("hw_p1_init failed");
		return (-ESPI);
	}

	pr_log(_("Firmware version %d detected"), ret);
	hw->run.fwversion = ret;
	hw->run.initialized = true;
	hw_p1_lcd_init();

	return (ALL_OK);
}

/**
 * Get the hardware ready for run loop.
 * Calibrate, restore hardware state from permanent storage.
 * @note this function currently checks that nsamples and nsensors
 * are set, thus making it currently impossible to run the prototype
 * hardware without sensors.
 * @param priv private hardware data
 * @return exec status
 */
static int hw_p1_online(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	int ret;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.initialized)
		return (-EINIT);

	if (!hw->set.nsamples)
		return (-EMISCONFIGURED);

	if (!hw->settings.nsensors)
		return (-EMISCONFIGURED);

	// save settings - for deffail
	ret = hw_p1_hwconfig_commit();
	if (ret)
		goto fail;

	// calibrate
	ret = hw_p1_calibrate();
	if (ret)
		goto fail;

	// restore previous state - failure is ignored
	ret = hw_p1_restore_relays();
	if (ALL_OK == ret)
		pr_log(_("Hardware state restored"));

	hw_p1_lcd_online();

	timer_add_cb(LOG_INTVL_TEMPS, hw_p1_async_log_temps, "log hw_p1 temps");

	hw->run.online = true;
	ret = ALL_OK;

fail:
	return (ret);
}

/**
 * Collect inputs from hardware.
 * @note Will process switch inputs.
 * @note Will panic if sensors cannot be read for more than 30s (hardcoded).
 * @param priv private hardware data
 * @return exec status
 * @todo review logic
 */
static int hw_p1_input(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	struct s_runtime * const runtime = get_runtime();
	static rwchc_sensor_t rawsensors[RWCHC_NTSENSORS];
	static unsigned int count = 0, systout = 0;
	static sid_t tempid = 1;
	static enum e_systemmode cursysmode = SYS_UNKNOWN;
	static bool syschg = false;
	int ret;

	assert(hw);

	if (!hw->run.online)
		return (-EOFFLINE);

	// read peripherals
	ret = hw_p1_rwchcperiphs_read();
	if (ALL_OK != ret) {
		dbgerr("hw_p1_rwchcperiphs_read failed (%d)", ret);
		goto skip_periphs;
	}

	// detect hardware alarm condition
	if (hw->peripherals.i_alarm) {
		pr_log(_("Hardware in alarm"));
		// clear alarm
		hw->peripherals.i_alarm = 0;
		hw_p1_lcd_reset();
		// XXX reset runtime?
	}

	// handle software alarm
	if (alarms_count()) {
		hw->peripherals.o_LED2 = 1;
		hw->peripherals.o_buzz = !hw->peripherals.o_buzz;
		count = 2;
	}
	else {
		hw->peripherals.o_LED2 = 0;
		hw->peripherals.o_buzz = 0;
	}

	// handle switch 1
	if (hw->peripherals.i_SW1) {
		hw->peripherals.i_SW1 = 0;
		count = 5;
		systout = 3;
		syschg = true;

		cursysmode++;

		if (cursysmode >= SYS_UNKNOWN)	// last valid mode
			cursysmode = 0;		// first valid mode

		hw_p1_lcd_sysmode_change(cursysmode);	// update LCD
	}

	if (!systout) {
		if (syschg && (cursysmode != runtime->systemmode)) {
			// change system mode
			pthread_rwlock_wrlock(&runtime->runtime_rwlock);
			runtime_set_systemmode(cursysmode);
			pthread_rwlock_unlock(&runtime->runtime_rwlock);
			// hw_p1_beep()
			hw->peripherals.o_buzz = 1;
		}
		syschg = false;
		cursysmode = runtime->systemmode;
	}
	else
		systout--;

	// handle switch 2
	if (hw->peripherals.i_SW2) {
		// increase displayed tempid
		tempid++;
		hw->peripherals.i_SW2 = 0;
		count = 5;

		if (tempid > hw->settings.nsensors)
			tempid = 1;

		hw_p1_lcd_set_tempid(tempid);	// update sensor
	}

	// trigger timed backlight
	if (count) {
		hw->peripherals.o_LCDbl = 1;
		if (!--count)
			hw_p1_lcd_fade();	// apply fadeout
	}
	else
		hw->peripherals.o_LCDbl = 0;

skip_periphs:
	// calibrate
	ret = hw_p1_calibrate();
	if (ALL_OK != ret) {
		dbgerr("hw_p1_calibrate failed (%d)", ret);
		goto fail;
		/* repeated calibration failure might signal a sensor acquisition circuit
		 that's broken. Temperature readings may no longer be reliable and
		 the system should eventually trigger failsafe */
	}

	// read sensors
	ret = hw_p1_sensors_read(rawsensors);
	if (ALL_OK != ret) {
		// flag the error but do NOT stop processing here
		dbgerr("hw_p1_sensors_read failed (%d)", ret);
		goto fail;
	}
	else {
		// copy valid data to local environment
		memcpy(hw->sensors, rawsensors, sizeof(hw->sensors));
		hw->run.sensors_ftime = time(NULL);
		hw_p1_parse_temps();
	}

	return (ret);

fail:
	if ((time(NULL) - hw->run.sensors_ftime) > 30) {
		// if we failed to read the sensor for too long, time to panic - XXX hardcoded
		alarms_raise(ret, _("Couldn't read sensors for more than 30s"), _("Sensor rd fail!"));
	}

	return (ret);
}

/**
 * Apply commands to hardware.
 * @param priv private hardware data
 * @return exec status
 */
static int hw_p1_output(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	int ret;

	assert(hw);

	if (!hw->run.online)
		return (-EOFFLINE);

	// update LCD
	ret = hw_p1_lcd_run();
	if (ALL_OK != ret)
		dbgerr("hw_p1_lcd_run failed (%d)", ret);

	// write relays
	ret = hw_p1_rwchcrelays_write();
	if (ALL_OK != ret) {
		dbgerr("hw_p1_rwchcrelays_write failed (%d)", ret);
		goto out;
	}

	// write peripherals
	ret = hw_p1_rwchcperiphs_write();
	if (ALL_OK != ret)
		dbgerr("hw_p1_rwchcperiphs_write failed (%d)", ret);

out:
	return (ret);
}

/**
 * Hardware offline routine.
 * Forcefully turns all relays off and saves final counters to permanent storage.
 * @param priv private hardware data
 * @return exec status
 */
static int hw_p1_offline(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	uint_fast8_t i;
	int ret;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.online)
		return (-EOFFLINE);

	hw_p1_lcd_offline();

	// turn off each known hardware relay
	for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
		if (!hw->Relays[i].set.configured)
			continue;

		hw->Relays[i].run.turn_on = false;
	}

	// update the hardware
	ret = hw_p1_rwchcrelays_write();
	if (ret)
		dbgerr("hw_p1_rwchcrelays_write failed (%d)", ret);

	// update permanent storage with final count
	hw_p1_save_relays();

	hw->run.online = false;

	return (ret);
}

/**
 * Hardware exit routine.
 * Resets the hardware.
 * @warning RESETS THE HARDWARE: no hardware operation after that call.
 * @param priv private hardware data
 */
static void hw_p1_exit(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;
	int ret;
	uint_fast8_t i;

	if (!hw)
		return;

	if (hw->run.online) {
		dbgerr("hardware is still online!");
		return;
	}

	if (!hw->run.initialized)
		return;

	hw_p1_lcd_exit();

	// cleanup all resources
	for (i = 1; i <= ARRAY_SIZE(hw->Relays); i++)
		hw_p1_setup_relay_release(i);

	// deconfigure all sensors
	for (i = 1; i <= ARRAY_SIZE(hw->Sensors); i++)
		hw_p1_setup_sensor_deconfigure(i);

	// reset the hardware
	ret = hw_p1_spi_reset();
	if (ret)
		dbgerr("reset failed (%d)", ret);

	hw->run.initialized = false;
}

/**
 * Return relay name.
 * @param priv private hardware data
 * @param id id of the target internal relay
 * @return target relay name or NULL if error
 */
static const char * hw_p1_relay_name(void * priv, const rid_t id)
{
	struct s_hw_p1_pdata * const hw = priv;

	assert(hw);

	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (NULL);

	return (hw->Relays[id-1].name);
}

/**
 * Set internal relay state (request)
 * @param priv private hardware data
 * @param id id of the internal relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hw_p1_rwchcrelays_write()
 */
static int hw_p1_relay_set_state(void * priv, const rid_t id, const bool turn_on, const time_t change_delay)
{
	struct s_hw_p1_pdata * const hw = priv;
	const time_t now = time(NULL);
	struct s_hw_p1_relay * relay = NULL;

	assert(hw);

	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	relay = &hw->Relays[id-1];

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state state request if delay permits
	if (turn_on) {
		if (!relay->run.is_on) {
			if ((now - relay->run.off_since) < change_delay)
				return (change_delay - (now - relay->run.off_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = true;
		}
	}
	else {	// turn off
		if (relay->run.is_on) {
			if ((now - relay->run.on_since) < change_delay)
				return (change_delay - (now - relay->run.on_since));	// don't do anything if previous state hasn't been held long enough - return remaining time

			relay->run.turn_on = false;
		}
	}

	return (ALL_OK);
}

/**
 * Get internal relay state (request).
 * Updates run.state_time and returns current state
 * @param priv private hardware data
 * @param id id of the internal relay to modify
 * @return run.is_on
 */
static int hw_p1_relay_get_state(void * priv, const rid_t id)
{
	struct s_hw_p1_pdata * const hw = priv;
	const time_t now = time(NULL);
	struct s_hw_p1_relay * relay = NULL;

	assert(hw);

	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	relay = &hw->Relays[id-1];

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (relay->run.is_on);
}

/**
 * Return sensor name.
 * @param priv private hardware data
 * @param id id of the target internal sensor
 * @return target sensor name or NULL if error
 */
static const char * hw_p1_sensor_name(void * priv, const sid_t id)
{
	struct s_hw_p1_pdata * const hw = priv;

	assert(hw);

	if ((!id) || (id > hw->settings.nsensors) || (id > ARRAY_SIZE(hw->Sensors)))
		return (NULL);

	return (hw->Sensors[id-1].name);
}

/**
 * Clone sensor temperature.
 * This function checks that the provided hardware id is valid, that is that it
 * is within boundaries of the hardware limits and the configured number of sensors.
 * It also checks that the designated sensor is properly configured in software.
 * Finally, if parameter @b tclone is non-null, the temperature of the sensor
 * is copied if it isn't stale (i.e. less than 30s old).
 * @todo review hardcoded timeout
 * @param priv private hardware data
 * @param id target sensor id
 * @param tclone optional location to copy the sensor temperature.
 * @return exec status
 */
int hw_p1_sensor_clone_temp(void * priv, const sid_t id, temp_t * const tclone)
{
	struct s_hw_p1_pdata * const hw = priv;
	int ret;
	temp_t temp;

	assert(hw);

	if ((id <= 0) || (id > hw->settings.nsensors) || (id > ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	if (!hw->Sensors[id-1].set.configured)
		return (-ENOTCONFIGURED);

	// make sure available data is valid - XXX 30s timeout hardcoded
	if ((time(NULL) - hw->run.sensors_ftime) > 30) {
		if (tclone)
			*tclone = 0;
		return (-EHARDWARE);
	}

	pthread_rwlock_rdlock(&hw->Sensors_rwlock);
	temp = hw->Sensors[id-1].run.value;
	pthread_rwlock_unlock(&hw->Sensors_rwlock);

	if (tclone)
		*tclone = temp;

	switch (temp) {
		case TEMPUNSET:
			ret = -ESENSORINVAL;
			break;
		case TEMPSHORT:
			ret = -ESENSORSHORT;
			break;
		case TEMPDISCON:
			ret = -ESENSORDISCON;
			break;
		case TEMPINVALID:
			ret = -EINVALID;
			break;
		default:
			ret = ALL_OK;
			break;
	}

	return (ret);
}

/**
 * Clone sensor last update time.
 * This function checks that the provided hardware id is valid, that is that it
 * is within boundaries of the hardware limits and the configured number of sensors.
 * It also checks that the designated sensor is properly configured in software.
 * Finally, if parameter @b ctime is non-null, the time of the last sensor update
 * is copied.
 * @param priv private hardware data
 * @param id target sensor id
 * @param ctime optional location to copy the sensor update time.
 * @return exec status
 */
static int hw_p1_sensor_clone_time(void * priv, const sid_t id, time_t * const ctime)
{
	struct s_hw_p1_pdata * const hw = priv;

	assert(hw);

	if ((id <= 0) || (id > hw->settings.nsensors) || (id > ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	if (!hw->Sensors[id-1].set.configured)
		return (-ENOTCONFIGURED);

	if (ctime)
		*ctime = hw->run.sensors_ftime;

	return (ALL_OK);
}

/**
 * Find sensor id by name.
 * @param priv unused
 * @param name target name to look for
 * @return error if not found or sensor id
 */
static int hw_p1_sensor_ibn(void * priv, const char * const name)
{
	if (!name)
		return (-EINVALID);

	return (hw_p1_sid_by_name(name));
}

/**
 * Find relay id by name.
 * @param priv unused
 * @param name target name to look for
 * @return error if not found or sensor id
 */
static int hw_p1_relay_ibn(void * priv, const char * const name)
{
	if (!name)
		return (-EINVALID);

	return (hw_p1_rid_by_name(name));
}

/** Hardware callbacks for Prototype 1 hardware */
static struct s_hw_callbacks hw_p1_callbacks = {
	.init = hw_p1_init,
	.exit = hw_p1_exit,
	.online = hw_p1_online,
	.offline = hw_p1_offline,
	.input = hw_p1_input,
	.output = hw_p1_output,
	.sensor_clone_temp = hw_p1_sensor_clone_temp,
	.sensor_clone_time = hw_p1_sensor_clone_time,
	.relay_get_state = hw_p1_relay_get_state,
	.relay_set_state = hw_p1_relay_set_state,
	.sensor_ibn = hw_p1_sensor_ibn,
	.relay_ibn = hw_p1_relay_ibn,
	.sensor_name = hw_p1_sensor_name,
	.relay_name = hw_p1_relay_name,
	.filecfg_dump = hw_p1_filecfg_dump,
};

/**
 * Backend register wrapper.
 * @param priv private hardware data
 * @param name user-defined name
 * @return exec status
 */
int hw_p1_backend_register(void * priv, const char * const name)
{
	if (!priv)
		return (-EINVALID);

	return (hw_backends_register(&hw_p1_callbacks, priv, name));
}
