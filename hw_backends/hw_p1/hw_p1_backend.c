//
//  hw_backends/hw_p1/hw_p1_backend.c
//  rwchcd
//
//  (C) 2018,2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 backend implementation.
 */

#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "hw_backends/hw_backends.h"
#include "hw_p1.h"
#include "hw_p1_spi.h"
#include "hw_p1_lcd.h"
#include "hw_p1_setup.h"
#include "hw_p1_filecfg.h"
#include "runtime.h"
#include "alarms.h"
#include "log/log.h"
#include "timekeep.h"
#include "lib.h"

#define INIT_MAX_TRIES		10	///< how many times hardware init should be retried
#define HW_P1_TIMEOUT_TK	(30 * TIMEKEEP_SMULT)	///< hardcoded hardware timeout delay: 30s

/**
 * Initialize hardware and ensure connection is set (needs root)
 * @param priv private hardware data
 * @param name user-set name for this backend
 * @return error state
 */
__attribute__((warn_unused_result)) static int hw_p1_setup(void * priv, const char * name)
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
		pr_err(_("HWP1 \"%s\": could not connect"), name);
		return (-ESPI);
	}

	pr_log(_("HWP1 \"%s\": Firmware version %d detected"), name, ret);
	hw->run.fwversion = ret;
	hw->name = name;
	hw->run.initialized = true;

	return (ALL_OK);
}

/**
 * Get the hardware ready for run loop.
 * Calibrate, restore hardware state from permanent storage.
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

	// save settings - for deffail and active sensors
	ret = hw_p1_hwconfig_commit(hw);
	if (ALL_OK != ret) {
		pr_err(_("HWP1 \"%s\": failed to update hardware config (%d)"), hw->name, ret);
		goto fail;
	}

	ret = hw_p1_refs_read(hw);
	if (ALL_OK != ret) {
		pr_err(_("HWP1 \"%s\": failed to read calibration data (%d)"), hw->name, ret);
		goto fail;
	}

	timekeep_sleep(1);	// wait for all sensors to be parsed by the hardware

	// read sensors once
	ret = hw_p1_sensors_read(hw);
	if (ALL_OK != ret) {
		pr_err(_("HWP1 \"%s\": could not read sensors (%d)"), hw->name, ret);
		goto fail;
	}

	// restore previous state - failure is ignored
	ret = hw_p1_restore_relays(hw);
	if (ALL_OK == ret)
		pr_log(_("HWP1 \"%s\": Hardware state restored"), hw->name);

	hw_p1_lcd_online(&hw->lcd);

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
 */
static int hw_p1_input(void * priv)
{
	struct s_hw_p1_pdata * restrict const hw = priv;
	int ret;

	assert(hw);

	if (!hw->run.online)
		return (-EOFFLINE);

	// read peripherals
	ret = hw_p1_rwchcperiphs_read(hw);
	if (ALL_OK != ret) {
		dbgerr("\"%s\" hw_p1_rwchcperiphs_read failed (%d)", hw->name, ret);
		goto skip_periphs;
	}

	// detect hardware alarm condition
	if (hw->peripherals.i_alarm) {
		pr_log(_("HWP1 \"%s\": Hardware in alarm"), hw->name);
		// clear alarm
		hw->peripherals.i_alarm = 0;
		hw_p1_lcd_reset(&hw->lcd);
		// XXX reset runtime?
	}

	// handle switch 1
	if (hw->peripherals.i_SW1) {
		hw->peripherals.i_SW1 = 0;
		hw->run.count = 5;
		hw->run.systout = 3;
		hw->run.syschg = true;

		hw->run.cursysmode++;

		if (hw->run.cursysmode >= SYS_UNKNOWN)	// last valid mode
			hw->run.cursysmode = SYS_NONE + 1;	// first valid mode

		hw_p1_lcd_sysmode_change(&hw->lcd, hw->run.cursysmode);	// update LCD
	}

	// handle switch 2
	if (hw->peripherals.i_SW2) {
		// increase displayed tempid
		hw->run.tempid++;
		hw->peripherals.i_SW2 = 0;
		hw->run.count = 5;

		if (hw->run.tempid >= hw->run.nsensors)
			hw->run.tempid = 0;

		hw_p1_lcd_set_tempid(&hw->lcd, hw->run.tempid);	// update sensor
	}

skip_periphs:
	// read sensors
	ret = hw_p1_sensors_read(hw);
	if (ALL_OK != ret) {
		// flag the error but do NOT stop processing here
		dbgerr("\"%s\" hw_p1_sensors_read failed (%d)", hw->name, ret);
		goto fail;
	}

	return (ret);

fail:
	if ((timekeep_now() - hw->run.sensors_ftime) >= HW_P1_TIMEOUT_TK) {
		// if we failed to read the sensor for too long, time to panic - XXX hardcoded
		alarms_raise(ret, _("HWP1 \"%s\": Couldn't read sensors: timeout exceeded!"), hw->name);
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
	if (ALL_OK != ret) {
		dbgerr("\"%s\" hw_p1_lcd_run failed (%d)", hw->name, ret);
	}
	// write relays
	ret = hw_p1_rwchcrelays_write(hw);
	if (ALL_OK != ret) {
		dbgerr("\"%s\" hw_p1_rwchcrelays_write failed (%d)", hw->name, ret);
		goto out;
	}

	// handle software alarm
	if (alarms_count()) {
		hw->peripherals.o_LED2 = 1;
		hw->peripherals.o_buzz = !hw->peripherals.o_buzz;
		hw->run.count = 2;
	}
	else {
		hw->peripherals.o_LED2 = 0;
		hw->peripherals.o_buzz = 0;
	}

	// handle mode changes
	if (!hw->run.systout) {
		if (hw->run.syschg && (hw->run.cursysmode != runtime_systemmode())) {
			// change system mode
			runtime_set_systemmode(hw->run.cursysmode);
			// hw_p1_beep()
			hw->peripherals.o_buzz = 1;
		}
		hw->run.syschg = false;
		hw->run.cursysmode = runtime_systemmode();
	}
	else
		hw->run.systout--;

	// trigger timed backlight
	if (hw->run.count) {
		hw->peripherals.o_LCDbl = 1;
		if (!--hw->run.count)
			hw_p1_lcd_fade(&hw->spi);	// apply fadeout
	}
	else
		hw->peripherals.o_LCDbl = 0;

	// write peripherals
	ret = hw_p1_rwchcperiphs_write(hw);
	if (ALL_OK != ret) {
		dbgerr("\"%s\" hw_p1_rwchcperiphs_write failed (%d)", hw->name, ret);
	}
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
		pr_err("HWP1 \"%s\": Offlining: Failed to update hardware (%d)", hw->name, ret);

	// update permanent storage with final count
	hw_p1_save_relays(hw);

	hw->run.online = false;

	// reset the hardware
	ret = hw_p1_spi_reset(&hw->spi);
	if (ret)
		pr_err("HWP1 \"%s\": Offlining: Failed to reset hardware (%d)", hw->name, ret);

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
		dbgerr("\"%s\" hardware is still online!", hw->name);
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
			str = ((inid >= hw->run.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors))) ? NULL : hw->Sensors[inid].name;
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
			if (unlikely((inid >= hw->run.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors))))
				return (-EINVALID);
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
			if ((inid >= hw->run.nsensors) || (inid >= ARRAY_SIZE(hw->Sensors)))
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
#ifdef HAS_FILECFG
	.filecfg_dump = hw_p1_filecfg_dump,
#endif
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
