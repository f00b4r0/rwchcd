//
//  hw_p1_setup.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 setup implementation.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset/strdup

#include "hw_p1_setup.h"

/**
 * Initialize local data.
 * Cannot fail.
 * @return #Hardware
 */
void * hw_p1_setup_new(void)
{
	memset(&Hardware, 0x0, sizeof(Hardware));
	pthread_rwlock_init(&Hardware.Sensors_rwlock, NULL);

	return (&Hardware);
}

/**
 * Set hardware configuration for LCD backlight level.
 * @param percent backlight level (0 = off, 100 = full)
 * @return exec status
 */
int hw_p1_setup_setbl(const uint8_t percent)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;

	if (percent > 100)
		return (-EINVALID);

	hw->settings.lcdblpct = percent;

	return (ALL_OK);
}

/**
 * Set hardware configuration for number of sensors.
 * @param lastid last connected sensor id
 * @return exec status
 */
int hw_p1_setup_setnsensors(const rid_t lastid)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;

	if ((lastid <= 0) || (lastid > RWCHC_NTSENSORS))
		return (-EINVALID);

	hw->settings.nsensors = lastid;

	return (ALL_OK);
}

/**
 * Set number of temperature samples for readouts.
 * @param nsamples number of samples
 * @return exec status
 */
int hw_p1_setup_setnsamples(const uint_fast8_t nsamples)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;

	if (!nsamples)
		return (-EINVALID);

	hw->set.nsamples = nsamples;

	return (ALL_OK);
}

/**
 * Configure a temperature sensor.
 * @param id the physical id of the sensor to configure (starting from 1)
 * @param type the sensor type (PT1000...)
 * @param offset a temperature offset to apply to this particular sensor value
 * @param name @b unique user-defined name describing the sensor
 * @return exec status
 */
int hw_p1_setup_sensor_configure(const sid_t id, const enum e_hw_p1_stype type, const temp_t offset, const char * const name)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;
	char * str = NULL;

	if (!id || (id > ARRAY_SIZE(hw->Sensors)) || !name)
		return (-EINVALID);

	if (hw->Sensors[id-1].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_sid_by_name(name) > 0)
		return (-EEXISTS);

	str = strdup(name);
	if (!str)
		return(-EOOM);

	hw->Sensors[id-1].ohm_to_celsius = hw_p1_sensor_o_to_c(type);

	if (!hw->Sensors[id-1].ohm_to_celsius)
		return (-EINVALID);

	hw->Sensors[id-1].name = str;
	hw->Sensors[id-1].set.type = type;
	hw->Sensors[id-1].set.offset = offset;
	hw->Sensors[id-1].set.configured = true;

	return (ALL_OK);
}

/**
 * Deconfigure a temperature sensor.
 * @param id the physical id of the sensor to deconfigure (starting from 1)
 * @return exec status
 */
int hw_p1_setup_sensor_deconfigure(const sid_t id)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;

	if (!id || (id > ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	if (!hw->Sensors[id-1].set.configured)
		return (-ENOTCONFIGURED);

	free(hw->Sensors[id-1].name);

	memset(&hw->Sensors[id-1], 0x00, sizeof(hw->Sensors[id-1]));

	return (ALL_OK);
}

/**
 * Request a hardware relay.
 * Ensures that the desired hardware relay is available and grabs it.
 * @param id target relay id (starting from 1)
 * @param failstate the state assumed by the hardware relay in standalone failover (controlling software failure)
 * @param name @b unique user-defined name for this relay (string will be copied locally)
 * @return exec status
 */
int hw_p1_setup_relay_request(const rid_t id, const bool failstate, const char * const name)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;
	char * str = NULL;

	if (!id || (id > ARRAY_SIZE(hw->Relays)) || !name)
		return (-EINVALID);

	if (hw->Relays[id-1].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_rid_by_name(name) > 0)
		return (-EEXISTS);

	str = strdup(name);
	if (!str)
		return(-EOOM);

	hw->Relays[id-1].name = str;

	// register failover state
	hw->Relays[id-1].set.failstate = failstate;

	hw->Relays[id-1].run.off_since = time(NULL);	// XXX hack

	hw->Relays[id-1].set.configured = true;

	return (ALL_OK);
}

/**
 * Release a hardware relay.
 * Frees and cleans up the target hardware relay.
 * @param id target relay id (starting from 1)
 * @return exec status
 */
int hw_p1_setup_relay_release(const rid_t id)
{
	struct s_hw_p1_pdata * restrict const hw = &Hardware;

	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	if (!hw->Relays[id-1].set.configured)
		return (-ENOTCONFIGURED);

	free(hw->Relays[id-1].name);

	memset(&hw->Relays[id-1], 0x00, sizeof(hw->Relays[id-1]));

	return (ALL_OK);
}
