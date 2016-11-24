//
//  rwchcd_hardware.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware interface implementation.
 */

#include <time.h>	// time
#include <math.h>	// sqrtf
#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <unistd.h>	// sleep

#include "rwchcd.h"
#include "rwchcd_spi.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lib.h"
#include "rwchcd_storage.h"
#include "rwchcd_lcd.h"
#include "rwchcd_hardware.h"

#include "rwchc_export.h"

#if RWCHC_NTSENSORS != RWCHCD_NTEMPS
#error Discrepancy in number of hardware sensors
#endif

#define RELAY_MAX_ID	14	///< maximum valid relay id

#define VALID_CALIB_MIN	0.8F	///< minimum valid calibration value
#define VALID_CALIB_MAX	1.2F	///< maximum valid calibration value

static const storage_version_t Hardware_sversion = 1;

static struct s_stateful_relay * Relays[RELAY_MAX_ID];	///< physical relays

static struct {
	float calib_nodac;		///< sensor calibration value without dac offset
	float calib_dac;		///< sensor calibration value with dac offset
	struct rwchc_s_settings settings;
	union rwchc_u_relays relays;		// XXX locks
	union rwchc_u_outperiphs peripherals;	// XXX locks
	rwchc_sensor_t sensors[RWCHC_NTSENSORS];	// XXX locks
} Hardware;

/**
 * Convert sensor value to actual resistance.
 * voltage on ADC pin is Vsensor * (1+G) - Vdac * G where G is divider gain on AOP.
 * if value < ~10mv: short. If value = max: open.
 * @param raw the raw sensor value
 * @param calib 1 if calibrated value is required, 0 otherwise
 * @return the resistance value
 */
static unsigned int sensor_to_ohm(const rwchc_sensor_t raw, const bool calib)
{
	const uint_fast16_t dacset[] = RWCHC_DAC_STEPS;
	uint_fast16_t value, dacoffset;
	float calibmult;

	dacoffset = (raw >> 12) & 0x3;

	value = raw & RWCHC_ADC_MAXV;		// raw is 10bit, cannot be negative when cast to sint
	value *= RWCHC_ADC_MVSCALE;		// convert to millivolts
	value += dacset[dacoffset]*RWCHC_DAC_MVSCALE*RWCHC_ADC_OPGAIN;	// add the initial offset

	/* value is now (1+RWCHC_ADC_OPGAIN) * actual value at sensor. Sensor is fed 0.5mA,
	 * so sensor resistance is 1/2 actual value in millivolt. 1+RWCHC_ADC_OPGAIN = 4.
	 * Thus, resistance in ohm is value/2 */

	value /= 2;

	// finally, apply calibration factor
	if (calib)
		calibmult = dacoffset ? Hardware.calib_dac : Hardware.calib_nodac;
	else
		calibmult = 1.0;

	value = ((float)value * calibmult);	// calibrate

	return (value);
}

// http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/platinum-rtd-sensors/resistance-calibration-table

/**
 * Convert Pt1000 resistance value to actual temperature.
 * Use a quadratic fit for simplicity.
 * @param ohm the resistance value to convert
 * @return temperature in Celsius
 */
static float pt1000_ohm_to_celsius(const uint_fast16_t ohm)
{
	const float R0 = 1000.0F;
	float alpha, delta, A, B, temp;

	// manufacturer parameters
	alpha = 0.003850F;	// mean R change referred to 0C
	//beta = 0.10863F;
	delta = 1.4999F;

	// Callendar - Van Dusen parameters
	A = alpha + (alpha * delta) / 100;
	B = (-alpha * delta) / (100 * 100);
	//C = (-alpha * beta) / (100 * 100 * 100 * 100);	// only for t < 0

	// quadratic fit: we're going to ignore the cubic term given the temperature range we're looking at
	temp = (-R0*A + sqrtf(R0*R0*A*A - 4*R0*B*(R0 - ohm))) / (2*R0*B);

	return (temp);
}

/**
 * Return a calibrated temp_t value for the given raw sensor data.
 * @param raw the raw sensor data to convert
 * @return the temperature in temp_t units
 * XXX REVISIT calls depth.
 */
static inline temp_t sensor_to_temp(const rwchc_sensor_t raw)
{
	return (celsius_to_temp(pt1000_ohm_to_celsius(sensor_to_ohm(raw, 1))));
}

/**
 * Process raw sensor data and extract temperature values into the runtime temps[] array.
 * Applies a short-window LP filter on raw data to smooth out noise.
 */
static void parse_temps(void)
{
	struct s_runtime * const runtime = get_runtime();
	static time_t lasttime = 0;	// in temp_expw_mavg, this makes alpha ~ 1, so the return value will be (prev value - 1*(0)) == prev value. Good
	const time_t dt = time(NULL) - lasttime;
	uint_fast8_t i;
	temp_t previous, current;
	
	for (i = 0; i<runtime->config->nsensors; i++) {
		current = sensor_to_temp(Hardware.sensors[i]);
		previous = runtime->temps[i];
		
		// apply LP filter with 5s time constant
		runtime->temps[i] = temp_expw_mavg(previous, current, 5, dt);
	}
	
	lasttime = time(NULL);
}

/**
 * Save hardware relays state to permanent storage
 * @return exec status
 */
static int hardware_save_relays(void)
{
	static uint8_t blob[ARRAY_SIZE(Relays)*sizeof(*Relays[0])];
	unsigned int i;
	
	for (i=0; i<ARRAY_SIZE(Relays); i++) {
		if (Relays[i])
			memcpy(&blob[i*sizeof(*Relays[0])], Relays[i], sizeof(*Relays[0]));
		else
			memset(&blob[i*sizeof(*Relays[0])], 0x00, sizeof(*Relays[0]));
	}
	
	return (storage_dump("hardware_relays", &Hardware_sversion, blob, sizeof(blob)));
}

/**
 * Restore hardware relays state from permanent storage
 * Restores cycles and on/off total time counts for set relays.
 * @return exec status
 */
static int hardware_restore_relays(void)
{
	static uint8_t blob[ARRAY_SIZE(Relays)*sizeof(*Relays[0])];
	storage_version_t sversion;
	typeof(Relays[0]) relayptr = (typeof(relayptr))&blob;
	unsigned int i;
	
	// try to restore key elements of hardware
	if (storage_fetch("hardware_relays", &sversion, blob, sizeof(blob)) == ALL_OK) {
		if (Hardware_sversion != sversion)
			return (ALL_OK);	// XXX

		for (i=0; i<ARRAY_SIZE(Relays); i++) {
			if (Relays[i]) {
				Relays[i]->run.on_tottime += relayptr->run.on_tottime;
				Relays[i]->run.off_tottime += relayptr->run.off_tottime;
				Relays[i]->run.cycles += relayptr->run.cycles;
			}
			relayptr++;
		}
	}
	else
		dbgmsg("storage_fetch failed");

	return (ALL_OK);
}

static inline uint8_t rid_to_rwchcaddr(const int_fast8_t id)
{
	if (id < 8)
		return (id-1);
	else
		return (id);
}

/**
 * Set hardware config addresses.
 * @param address target hardware address
 * @param id target id
 * @return exec status
 * @warning minimal sanity check. HADDR_SLAST must be set first.
 */
int hardware_config_addr_set(enum e_hw_address address, const int_fast8_t id)
{
	uint8_t rid;
	
	// sanity checks
	if (id <= 0)
		return (-EINVALID);
	
	switch (address) {
		case HADDR_SLAST:
			if (id > RWCHCD_NTEMPS)
				return (-EINVALID);
			break;
		case HADDR_SBURNER:
		case HADDR_SWATER:
		case HADDR_SOUTDOOR:
			if (id > Hardware.settings.addresses.nsensors)
				return (-EINVALID);
			break;
		case HADDR_TBURNER:
		case HADDR_TPUMP:
		case HADDR_TVOPEN:
		case HADDR_TVCLOSE:
			if (id > RELAY_MAX_ID)
				return (-EINVALID);
			break;
		default:
			return (-EINVALID);
	}

	rid = rid_to_rwchcaddr(id);
	
	// apply setting
	switch (address) {
		case HADDR_SLAST:
			if (id > RWCHCD_NTEMPS)
				return (-EINVALID);
			Hardware.settings.addresses.nsensors = id;
			break;
		case HADDR_SBURNER:
			if (id > Hardware.settings.addresses.nsensors)
				return (-EINVALID);
			Hardware.settings.addresses.S_burner = id-1;
			break;
		case HADDR_SWATER:
			if (id > Hardware.settings.addresses.nsensors)
				return (-EINVALID);
			Hardware.settings.addresses.S_water = id-1;
			break;
		case HADDR_SOUTDOOR:
			if (id > Hardware.settings.addresses.nsensors)
				return (-EINVALID);
			Hardware.settings.addresses.S_outdoor = id-1;
			break;
		case HADDR_TBURNER:
			Hardware.settings.addresses.T_burner = rid;
			break;
		case HADDR_TPUMP:
			Hardware.settings.addresses.T_pump = rid;
			break;
		case HADDR_TVOPEN:
			Hardware.settings.addresses.T_Vopen = rid;
			break;
		case HADDR_TVCLOSE:
			Hardware.settings.addresses.T_Vclose = rid;
			break;
	}
	
	return (ALL_OK);
}

/**
 * Set hardware limit.
 * @param limit target hardware limit
 * @param value target limit value
 * @return exec status
 * @warning minimal sanity check.
 */
int hardware_config_limit_set(enum e_hw_limit limit, const int_fast8_t value)
{
	switch (limit) {
		case HLIM_FROSTMIN:
			Hardware.settings.limits.frost_tmin = value;
			break;
		case HLIM_BOILERMIN:
			Hardware.settings.limits.burner_tmin = value;
			break;
		case HLIM_BOILERMAX:
			Hardware.settings.limits.burner_tmax = value;
			break;
		default:
			return (-EINVALID);
	}
	
	return (ALL_OK);
}

/**
 * Read hardware config.
 * @param settings target hardware configuration
 * @return exec status
 */
static int hardware_config_fetch(struct rwchc_s_settings * const settings)
{
	int ret, i = 0;
	
	// grab current config from the hardware
	do {
		ret = rwchcd_spi_settings_r(settings);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	return (ret);
}

/**
 * Commit and save hardware config.
 * @param settings target hardware configuration
 * @return exec status
 */
int hardware_config_store(void)
{
	struct rwchc_s_settings hw_set;
	int ret, i = 0;
	
	// grab current config from the hardware
	hardware_config_fetch(&hw_set);
	
	if (!memcmp(&hw_set, &(Hardware.settings), sizeof(hw_set)))
		return (ALL_OK); // don't wear flash down if unnecessary
	
	// commit hardware config
	do {
		ret = rwchcd_spi_settings_w(&(Hardware.settings));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	if (ret)
		goto out;
	
	i = 0;
	// save hardware config
	do {
		ret = rwchcd_spi_settings_s();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));
	
	dbgmsg("HW Config saved.");
	
out:
	return (ret);
}

/**
 * Initialize hardware and ensure connection is set
 * @return error state
 */
int hardware_init(void)
{
	int ret;
	
	if (rwchcd_spi_init() < 0)
		return (-EINIT);

	memset(Relays, 0x0, ARRAY_SIZE(Relays));
	memset(&Hardware, 0x0, sizeof(Hardware));
	
	// fetch hardware config
	ret = hardware_config_fetch(&(Hardware.settings));
	if (ret)
		dbgerr("hardware_config_fetch failed");

	return (ret);
}

/**
 * Calibrate hardware readouts.
 * Calibrate both with and without DAC offset. Must be called before any temperature is to be read.
 * @return error status
 * @note rwchcd_spi_calibrate() sleeps so this will sleep too, up to RWCHCD_SPI_MAX_TRIES times
 */
static int hardware_calibrate(void)
{
	uint_fast16_t refcalib, i;
	int ret = ALL_OK;
	rwchc_sensor_t ref;

	i = 0;
	do {
		ret = rwchcd_spi_calibrate();
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	i = 0;
	do {
		ret = rwchcd_spi_ref_r(&ref, 0);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		Hardware.calib_nodac = ((float)RWCHC_CALIB_OHM / (float)refcalib);	// calibrate against 1kohm reference
	}

	if ((Hardware.calib_nodac < VALID_CALIB_MIN) || (Hardware.calib_nodac > VALID_CALIB_MAX)) {
		ret = -EGENERIC;
		goto out;	// XXX should not happen
	}

	i = 0;
	do {
		ret = rwchcd_spi_ref_r(&ref, 1);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	if (ret)
		goto out;

	if (ref && ((ref & RWCHC_ADC_MAXV) < RWCHC_ADC_MAXV)) {
		refcalib = sensor_to_ohm(ref, 0);	// force uncalibrated read
		Hardware.calib_dac = ((float)RWCHC_CALIB_OHM / (float)refcalib);	// calibrate against 1kohm reference
	}

	if ((Hardware.calib_dac < VALID_CALIB_MIN) || (Hardware.calib_dac > VALID_CALIB_MAX))
		ret = -EGENERIC;	// XXX should not happen

out:
	return (ret);
}

/**
 * Read all sensors
 * @param tsensors the array to populate with current values
 * @param last the id of the last wanted (connected) sensor
 * @return exec status
 * @warning Hardware.settings.addresses.nsensors must be set prior to calling this function
 */
static int hardware_sensors_read(rwchc_sensor_t tsensors[])
{
	int_fast8_t sensor;
	int i, ret = ALL_OK;

	for (sensor=0; sensor<Hardware.settings.addresses.nsensors; sensor++) {
		i = 0;
		do {
			ret = rwchcd_spi_sensor_r(tsensors, sensor);
		} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

		if (ret)
			goto out;
	}

out:
	return (ret);
}

/**
 * Write all relays
 * This function updates all known hardware relays according to their desired turn_on
 * state. This function also does time and cycle accounting for the relays.
 * @note non-configured hardware relays are turned off.
 * @return status
 */
int hardware_rwchcrelays_write(void)
{
	struct s_stateful_relay * restrict relay;
	union rwchc_u_relays rWCHC_relays;
	const time_t now = time(NULL);	// we assume the whole thing will take much less than a second
	uint_fast8_t rid, i;
	bool change = false;
	int ret = -EGENERIC;

	// start clean
	rWCHC_relays.ALL = 0;

	// update each known hardware relay
	for (i=0; i<RELAY_MAX_ID; i++) {
		relay = Relays[i];

		if (!relay)
			continue;

		// update state counters at state change
		if (relay->run.turn_on) {	// turn on
			if (!relay->run.is_on) {	// relay is currently off
				relay->run.cycles++;	// increment cycle count
				relay->run.is_on = true;
				relay->run.on_since = now;
				if (relay->run.off_since)
					relay->run.off_tottime += now - relay->run.off_since;
				relay->run.off_since = 0;
				change = true;
			}
		}
		else {	// turn off
			if (relay->run.is_on) {	// relay is currently on
				relay->run.is_on = false;
				relay->run.off_since = now;
				if (relay->run.on_since)
					relay->run.on_tottime += now - relay->run.on_since;
				relay->run.on_since = 0;
				change = true;
			}
		}

		// update state time counter
		relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

		// extract relay id XXX REVISIT
		rid = relay->set.id - 1;
		if (rid > 6)
			rid++;	// skip the hole

		// set state for triac control
		if (relay->run.turn_on)
			setbit(rWCHC_relays.ALL, rid);
		else
			clrbit(rWCHC_relays.ALL, rid);
	}

	// save relays state if there was a change
	if (change) {
		ret = hardware_save_relays();
		if (ret)
			dbgerr("hardware_save failed (%d)", ret);
	}
	
	// send new state to hardware
	i = 0;
	do {
		ret = rwchcd_spi_relays_w(&rWCHC_relays);
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	// update internal runtime state on success
	// XXX NOTE there will be a discrepancy between internal state and Relays[] if the above fails
	if (ALL_OK == ret)
		Hardware.relays.ALL = rWCHC_relays.ALL;

	return (ret);
}

/**
 * Write all peripherals from internal runtime to hardware
 * @return status
 */
int hardware_rwchcperiphs_write(void)
{
	int i = 0, ret;

	do {
		ret = rwchcd_spi_peripherals_w(&(Hardware.peripherals));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Read all peripherals from hardware into internal runtime
 * @return exec status
 */
int hardware_rwchcperiphs_read(void)
{
	int i = 0, ret;

	do {
		ret = rwchcd_spi_peripherals_r(&(Hardware.peripherals));
	} while (ret && (i++ < RWCHCD_SPI_MAX_TRIES));

	return (ret);
}

/**
 * Create a new stateful relay
 * @return pointer to the created relay
 */
struct s_stateful_relay * hardware_relay_new(void)
{
	struct s_stateful_relay * const relay = calloc(1, sizeof(struct s_stateful_relay));

	// at creation relay is off
	if (relay)
		relay->run.off_since = time(NULL);

	return (relay);
}

/**
 * Delete a stateful relay.
 * @param relay the relay to delete
 * @note the deleted relay will be turned off by a call to _write()
 */
void hardware_relay_del(struct s_stateful_relay * relay)
{
	if (!relay)
		return;

	Relays[relay->set.id-1] = NULL;

	free(relay->name);
	relay->name = NULL;
	free(relay);
}

/**
 * Set a relay's id
 * @param relay the target relay
 * @param id the considered hardware id (numbered from 1)
 * @return exec status
 */
int hardware_relay_set_id(struct s_stateful_relay * const relay, const uint_fast8_t id)
{
	if (!relay)
		return (-EINVALID);

	if (!id || id > RELAY_MAX_ID)
		return (-EINVALID);

	if (Relays[id-1])
		return (-EEXISTS);

	relay->set.id = id;
	Relays[id-1] = relay;

	return (ALL_OK);
}

/**
 * set internal relay state (request)
 * @param relay the internal relay to modify
 * @param turn_on true if relay is meant to be turned on
 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
 * @return 0 on success, positive number for cooldown wait remaining, negative for error
 * @note actual (hardware) relay state will only be updated by a call to hardware_rwchcrelays_write()
 */
int hardware_relay_set_state(struct s_stateful_relay * const relay, const bool turn_on, const time_t change_delay)
{
	const time_t now = time(NULL);

	if (!relay)
		return (-EINVALID);

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
 * @param relay the internal relay to modify
 * @return run.is_on
 */
int hardware_relay_get_state(struct s_stateful_relay * const relay)
{
	const time_t now = time(NULL);

	if (!relay)
		return (-EINVALID);

	if (!relay->set.configured)
		return (-ENOTCONFIGURED);

	// update state time counter
	relay->run.state_time = relay->run.is_on ? (now - relay->run.on_since) : (now - relay->run.off_since);

	return (relay->run.is_on);
}

/**
 * Get the hardware ready for run loop.
 * Calibrate, then collect and process sensors.
 * @return exec status
 */
int hardware_online(void)
{
	struct s_runtime * const runtime = get_runtime();
	int ret;

	if (!runtime->config || !runtime->config->configured)	// for parse_temps()
		return (-ENOTCONFIGURED);

	// calibrate
	ret = hardware_calibrate();
	if (ret)
		goto fail;

	// restore previous state
	ret = hardware_restore_relays();
	if (ret)
		goto fail;
	
	// read sensors
	ret = hardware_sensors_read(Hardware.sensors);
	if (ret)
		goto fail;

	parse_temps();

fail:
	return (ret);
}

/**
 * Hardware run loop
 */
void hardware_run(void)
{
	struct s_runtime * const runtime = get_runtime();
	static rwchc_sensor_t rawsensors[RWCHC_NTSENSORS];
	static int count = 0;
	static tempid_t tempid = 1;
	enum e_systemmode cursysmode;
	int ret;

	if (!runtime->config || !runtime->config->configured) {
		dbgerr("not configured");
		return;	// XXX when this is a while(1){} thread this should be 'continue'
	}

	// fetch SPI data

	// read peripherals
	ret = hardware_rwchcperiphs_read();
	if (ret)
		dbgerr("hardware_rwchcperiphs_read failed (%d)", ret);

	if (Hardware.peripherals.LED2) {
		// clear alarm
		Hardware.peripherals.LED2 = 0;
		Hardware.peripherals.buzzer = 0;
		Hardware.peripherals.LCDbl = 0;
		lcd_update(true);
		// XXX reset runtime?
	}
	
	if (Hardware.peripherals.RQSW1) {
		// change system mode
		cursysmode = runtime->systemmode;
		cursysmode++;
		Hardware.peripherals.RQSW1 = 0;
		count = 5;
		
		if (cursysmode >= SYS_UNKNOWN)	// XXX last mode
			cursysmode = SYS_OFF;
		
		runtime_set_systemmode(cursysmode);	// XXX should only be active after timeout?
	}
	
	if (Hardware.peripherals.RQSW2) {
		// increase displayed tempid
		tempid++;
		Hardware.peripherals.RQSW2 = 0;
		count = 5;
		
		if (tempid > runtime->config->nsensors)
			tempid = 1;
	}
	
	if (count) {
		Hardware.peripherals.LCDbl = 1;
		count--;
		if (!count)
			lcd_fade();
	}
	else
		Hardware.peripherals.LCDbl = 0;
	
	lcd_line1(tempid);
	lcd_update(false);
	
	// read sensors
	ret = hardware_sensors_read(rawsensors);
	if (ret) {
		// XXX REVISIT: flag the error but do NOT stop processing here
		dbgerr("hardware_sensors_read failed: %d", ret);
	}
	else {
		// copy valid data to runtime environment
		memcpy(Hardware.sensors, rawsensors, sizeof(Hardware.sensors));
	}

	parse_temps();
	
	/* we want to release locks and sleep here to reduce contention and
	 * allow other parts to do their job before writing back */
	//sleep(1);

	// send SPI data

	// write relays
	ret = hardware_rwchcrelays_write();
	if (ret)
		dbgerr("hardware_rwchcrelays_write failed: %d", ret);

	// write peripherals
	ret = hardware_rwchcperiphs_write();
	if (ret)
		dbgerr("hardware_rwchcperiphs_write failed (%d)", ret);
}
