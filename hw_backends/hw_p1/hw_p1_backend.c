//
//  hw_backends/hw_p1/hw_p1_backend.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 backend implementation.
 */

#include <assert.h>
#include <string.h>
#include <stdatomic.h>

#include "hw_backends/hw_backends.h"
#include "hw_p1.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1_setup.h"
#include "hw_p1_filecfg.h"
#include "runtime.h"
#include "alarms.h"
#include "log/log.h"

#define INIT_MAX_TRIES		10	///< how many times hardware init should be retried
#define HW_P1_TIMEOUT_TK	(30 * TIMEKEEP_SMULT)	///< hardcoded hardware timeout delay: 30s

/**
 * HW P1 temperatures log callback.
 * @param ldata the log data to populate
 * @param object hw_p1 Hardware object
 * @return exec status
 */
static int hw_p1_temps_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_hw_p1_pdata * const hw = object;
	uint_fast8_t i = 0;

	assert(ldata);
	assert(ldata->nkeys >= RWCHC_NTSENSORS);

	if (!hw->run.online)
		return (-EOFFLINE);

	if (!hw->run.sensors_ftime)
		return (-EINVALID);	// data not ready

	for (i = 0; i < hw->settings.nsensors; i++)
		ldata->values[i] = aler(&hw->Sensors[i].run.value) + hw->Sensors[i].set.offset;

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for HW P1 temps.
 * @param hw HW P1 private data
 * @return (statically allocated) s_log_source pointer
 * @warning must not be called concurrently
 * @bug hardcoded basename/identifier will collide if multiple instances.
 */
static const struct s_log_source * hw_p1_lreg(const struct s_hw_p1_pdata * const hw)
{
	static const log_key_t keys[] = {
		"s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15",
	};
	static const enum e_log_metric metrics[] = {
		LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE,
	};
	static struct s_log_source HW_P1_temps_lsrc;

	HW_P1_temps_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_1mn,
		.basename = "hw_p1",
		.identifier = "temps",
		.version = 2,
		.nkeys = ARRAY_SIZE(keys),
		.keys = keys,
		.metrics = metrics,
		.logdata_cb = hw_p1_temps_logdata_cb,
		.object = hw,
	};
	return (&HW_P1_temps_lsrc);
}

/**
 * Initialize hardware and ensure connection is set (needs root)
 * @param priv private hardware data
 * @return error state
 */
__attribute__((warn_unused_result)) static int hw_p1_setup(void * priv)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	int ret, i = 0;

	if (!hw)
		return (-EINVALID);

	if (hw_p1_spi_setup(&hw->spi) < 0)
		return (-EINIT);

	// fetch firmware version
	do {
		ret = hw_p1_spi_fwversion(&hw->spi);
	} while ((ret <= 0) && (i++ < INIT_MAX_TRIES));

	if (ret <= 0) {
		pr_err(_("HWP1: could not connect"));
		return (-ESPI);
	}

	pr_log(_("HWP1: Firmware version %d detected"), ret);
	hw->run.fwversion = ret;
	hw->run.initialized = true;

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
	struct s_hw_p1_pdata * restrict const hw = priv;
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
	ret = hw_p1_hwconfig_commit(hw);
	if (ret)
		goto fail;

	// calibrate
	ret = hw_p1_calibrate(hw);
	if (ALL_OK != ret) {
		pr_err(_("HWP1: could not calibrate (%d)"), ret);
		goto fail;
	}

	// read sensors once
	ret = hw_p1_sensors_read(hw);
	if (ALL_OK != ret) {
		pr_err(_("HWP1: could not read sensors (%d)"), ret);
		goto fail;
	}

	// restore previous state - failure is ignored
	ret = hw_p1_restore_relays(hw);
	if (ALL_OK == ret)
		pr_log(_("HWP1: Hardware state restored"));

	hw_p1_lcd_online(&hw->lcd);

	log_register(hw_p1_lreg(hw));

	hw->run.online = true;
	ret = ALL_OK;

fail:
	return (ret);
}

/**
 * Collect inputs from hardware.
 * @note Will process switch inputs.
 * @note Will panic if sensors cannot be read for more than HW_P1_TIMEOUT_TK (hardcoded).
 * @param priv private hardware data
 * @return exec status
 * @todo review logic
 */
static int hw_p1_input(void * priv)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	static unsigned int count = 0, systout = 0;
	static uint_fast8_t tempid = 0;
	static enum e_systemmode cursysmode = SYS_UNKNOWN;
	static bool syschg = false;
	int ret;

	assert(hw);

	if (!hw->run.online)
		return (-EOFFLINE);

	// read peripherals
	ret = hw_p1_rwchcperiphs_read(hw);
	if (ALL_OK != ret) {
		dbgerr("hw_p1_rwchcperiphs_read failed (%d)", ret);
		goto skip_periphs;
	}

	// detect hardware alarm condition
	if (hw->peripherals.i_alarm) {
		pr_log(_("Hardware in alarm"));
		// clear alarm
		hw->peripherals.i_alarm = 0;
		hw_p1_lcd_reset(&hw->lcd);
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
			cursysmode = SYS_NONE + 1;	// first valid mode

		hw_p1_lcd_sysmode_change(&hw->lcd, cursysmode);	// update LCD
	}

	if (!systout) {
		if (syschg && (cursysmode != runtime_systemmode())) {
			// change system mode
			runtime_set_systemmode(cursysmode);
			// hw_p1_beep()
			hw->peripherals.o_buzz = 1;
		}
		syschg = false;
		cursysmode = runtime_systemmode();
	}
	else
		systout--;

	// handle switch 2
	if (hw->peripherals.i_SW2) {
		// increase displayed tempid
		tempid++;
		hw->peripherals.i_SW2 = 0;
		count = 5;

		if (tempid >= hw->settings.nsensors)
			tempid = 0;

		hw_p1_lcd_set_tempid(&hw->lcd, tempid);	// update sensor
	}

	// trigger timed backlight
	if (count) {
		hw->peripherals.o_LCDbl = 1;
		if (!--count)
			hw_p1_lcd_fade(&hw->spi);	// apply fadeout
	}
	else
		hw->peripherals.o_LCDbl = 0;

skip_periphs:
	// calibrate
	ret = hw_p1_calibrate(hw);
	if (ALL_OK != ret) {
		dbgerr("hw_p1_calibrate failed (%d)", ret);
		goto fail;
		/* repeated calibration failure might signal a sensor acquisition circuit
		 that's broken. Temperature readings may no longer be reliable and
		 the system should eventually trigger failsafe */
	}

	// read sensors
	ret = hw_p1_sensors_read(hw);
	if (ALL_OK != ret) {
		// flag the error but do NOT stop processing here
		dbgerr("hw_p1_sensors_read failed (%d)", ret);
		goto fail;
	}

	return (ret);

fail:
	if ((timekeep_now() - hw->run.sensors_ftime) >= HW_P1_TIMEOUT_TK) {
		// if we failed to read the sensor for too long, time to panic - XXX hardcoded
		alarms_raise(ret, _("Couldn't read sensors: timeout exceeded!"), _("Sensor rd fail!"));
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
	struct s_hw_p1_pdata * restrict const hw = priv;
	int ret;

	assert(hw);

	if (!hw->run.online)
		return (-EOFFLINE);

	// update LCD
	ret = hw_p1_lcd_run(&hw->lcd, &hw->spi, hw);
	if (ALL_OK != ret)
		dbgerr("hw_p1_lcd_run failed (%d)", ret);

	// write relays
	ret = hw_p1_rwchcrelays_write(hw);
	if (ALL_OK != ret) {
		dbgerr("hw_p1_rwchcrelays_write failed (%d)", ret);
		goto out;
	}

	// write peripherals
	ret = hw_p1_rwchcperiphs_write(hw);
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
	struct s_hw_p1_pdata * restrict const hw = priv;
	uint_fast8_t i;
	int ret;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.online)
		return (-EOFFLINE);

	log_deregister(hw_p1_lreg(hw));

	hw_p1_lcd_offline(&hw->lcd);

	// turn off each known hardware relay
	for (i=0; i<ARRAY_SIZE(hw->Relays); i++) {
		if (!hw->Relays[i].set.configured)
			continue;
		hw->Relays[i].run.turn_on = false;
	}

	// update the hardware
	ret = hw_p1_rwchcrelays_write(hw);
	if (ret)
		dbgerr("hw_p1_rwchcrelays_write failed (%d)", ret);

	// update permanent storage with final count
	hw_p1_save_relays(hw);

	hw->run.online = false;

	// reset the hardware
	ret = hw_p1_spi_reset(&hw->spi);
	if (ret)
		dbgerr("reset failed (%d)", ret);

	return (ret);
}

/**
 * Hardware exit routine.
 * Resets the hardware and frees all private memory.
 * @warning RESETS THE HARDWARE: no hardware operation after that call.
 * @param priv private hardware data. Will be invalid after the call.
 */
static void hw_p1_exit(void * priv)
{
	struct s_hw_p1_pdata * restrict const hw = priv;

	if (!hw)
		return;

	if (hw->run.online) {
		dbgerr("hardware is still online!");
		return;
	}

	if (!hw->run.initialized)
		return;

	hw->run.initialized = false;

	// delete private data created with hw_p1_setup_new()
	hw_p1_setup_del(hw);
}

/**
 * Return output name.
 * @param priv private hardware data
 * @param type the type of requested output
 * @param oid id of the target internal output
 * @return target output name or NULL if error
 */
static const char * hw_p1_output_name(void * const priv, const enum e_hw_output_type type, const outid_t oid)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_OUTPUT_RELAY:
			str = (oid >= ARRAY_SIZE(hw->Relays)) ? NULL : hw->Relays[oid].name;
			break;
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * Set internal output state (request)
 * @param priv private hardware data
 * @param type the type of requested output
 * @param oid id of the internal output to modify
 * @param state pointer to desired target state of the output
 * @return exec status
 * @note actual (hardware) relay state will only be updated by a call to hw_p1_rwchcrelays_write()
 */
static int hw_p1_output_state_set(void * const priv, const enum e_hw_output_type type, const outid_t oid, const u_hw_out_state_t * const state)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	struct s_hw_p1_relay * relay;

	assert(hw && state);

	switch (type) {
		case HW_OUTPUT_RELAY:
			if (unlikely(oid >= ARRAY_SIZE(hw->Relays)))
				return (-EINVALID);
			relay = &hw->Relays[oid];
			if (unlikely(!relay->set.configured))
				return (-ENOTCONFIGURED);
			relay->run.turn_on = state->relay;
			break;
		case HW_OUTPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Get internal output state (request).
 * @param priv private hardware data
 * @param type the type of requested output
 * @param oid id of the internal output to modify
 * @param state pointer in which the current state of the output will be stored
 * @return exec status
 */
static int hw_p1_output_state_get(void * const priv, const enum e_hw_output_type type, const outid_t oid, u_hw_out_state_t * const state)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	struct s_hw_p1_relay * relay;

	assert(hw && state);

	switch (type) {
		case HW_OUTPUT_RELAY:
			if (unlikely(oid >= ARRAY_SIZE(hw->Relays)))
				return (-EINVALID);
			relay = &hw->Relays[oid];
			if (unlikely(!relay->set.configured))
				return (-ENOTCONFIGURED);
			state->relay = relay->run.is_on;
			break;
		case HW_OUTPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Return input name.
 * @param priv private hardware data
 * @param type the type of requested input
 * @param inid id of the target internal input
 * @return target input name or NULL if error
 */
static const char * hw_p1_input_name(void * const priv, const enum e_hw_input_type type, const inid_t inid)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_INPUT_TEMP:
			str = ((inid >= hw->settings.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors))) ? NULL : hw->Sensors[inid].name;
			break;
		case HW_INPUT_SWITCH:
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * Get input value.
 * This function checks that the provided hardware id is valid, that is that it
 * is within boundaries of the hardware limits and the configured number of sensors.
 * It also checks that the designated sensor is properly configured in software.
 * Finally, the value of the input is copied if it isn't stale (i.e. less than HW_P1_TIMEOUT_TK old).
 * @param priv private hardware data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param value location to copy the current value of the input
 * @return exec status
 * @todo review hardcoded timeout.
 */
int hw_p1_input_value_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, u_hw_in_value_t * const value)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	const struct s_hw_p1_sensor * sensor;
	temp_t temp;
	int ret;

	assert(hw && value);

	switch (type) {
		case HW_INPUT_TEMP:
			if (unlikely((inid >= hw->settings.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors))))
				return (-EINVALID);
			// make sure available data is valid - XXX HW_P1_TIMEOUT_TK timeout hardcoded
			if (unlikely((timekeep_now() - hw->run.sensors_ftime) > HW_P1_TIMEOUT_TK))
				return (-EHARDWARE);
			sensor = &hw->Sensors[inid];
			if (!sensor->set.configured)
				return (-ENOTCONFIGURED);
			temp = aler(&sensor->run.value);
			value->temperature = temp + sensor->set.offset;

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
			break;
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ret);
}

/**
 * Get input last update time.
 * This function checks that the provided hardware id is valid, that is that it
 * is within boundaries of the hardware limits and the configured number of sensors.
 * It also checks that the designated sensor is properly configured in software.
 * @param priv private hardware data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param ctime location to copy the input update time.
 * @return exec status
 */
static int hw_p1_input_time_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, timekeep_t * const ctime)
{
	struct s_hw_p1_pdata * restrict const hw = priv;

	assert(hw && ctime);

	switch (type) {
		case HW_INPUT_TEMP:
			if ((inid >= hw->settings.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors)))
				return (-EINVALID);
			if (!hw->Sensors[inid].set.configured)
				return (-ENOTCONFIGURED);
			break;
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	*ctime = hw->run.sensors_ftime;

	return (ALL_OK);
}

/**
 * Find input id by name.
 * @param priv private hardware data
 * @param type the type of requested input
 * @param name target name to look for
 * @return error if not found or input id
 */
static int hw_p1_input_ibn(void * const priv, const enum e_hw_input_type type, const char * const name)
{
	const struct s_hw_p1_pdata * restrict const hw = priv;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_INPUT_TEMP:
			return (hw_p1_sid_by_name(hw, name));
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}
}

/**
 * Find output id by name.
 * @param priv private hardware data
 * @param type the type of requested output
 * @param name target name to look for
 * @return error if not found or output id
 */
static int hw_p1_output_ibn(void * const priv, const enum e_hw_output_type type, const char * const name)
{
	const struct s_hw_p1_pdata * restrict const hw = priv;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_OUTPUT_RELAY:
			return (hw_p1_rid_by_name(hw, name));
		case HW_OUTPUT_NONE:
		default:
			return (-EINVALID);
	}
}

/** Hardware callbacks for Prototype 1 hardware */
static const struct s_hw_callbacks hw_p1_callbacks = {
	.setup = hw_p1_setup,
	.exit = hw_p1_exit,
	.online = hw_p1_online,
	.offline = hw_p1_offline,
	.input = hw_p1_input,
	.output = hw_p1_output,
	.input_value_get = hw_p1_input_value_get,
	.input_time_get = hw_p1_input_time_get,
	.output_state_get = hw_p1_output_state_get,
	.output_state_set = hw_p1_output_state_set,
	.input_ibn = hw_p1_input_ibn,
	.output_ibn = hw_p1_output_ibn,
	.input_name = hw_p1_input_name,
	.output_name = hw_p1_output_name,
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
