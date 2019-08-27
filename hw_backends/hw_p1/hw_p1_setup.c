//
//  hw_backends/hw_p1/hw_p1_setup.c
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
#include <assert.h>

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
 * @param hw private hw_p1 hardware data
 * @param percent backlight level (0 = off, 100 = full)
 * @return exec status
 */
int hw_p1_setup_setbl(struct s_hw_p1_pdata * restrict const hw, const uint8_t percent)
{
	assert(hw);

	if (percent > 100)
		return (-EINVALID);

	hw->settings.lcdblpct = percent;

	return (ALL_OK);
}

/**
 * Set hardware configuration for number of sensors.
 * @param hw private hw_p1 hardware data
 * @param lastid last connected sensor id
 * @return exec status
 */
int hw_p1_setup_setnsensors(struct s_hw_p1_pdata * restrict const hw, const rid_t lastid)
{
	assert(hw);

	if ((lastid <= 0) || (lastid > RWCHC_NTSENSORS))
		return (-EINVALID);

	hw->settings.nsensors = lastid;

	return (ALL_OK);
}

/**
 * Set number of temperature samples for readouts.
 * @param hw private hw_p1 hardware data
 * @param nsamples number of samples
 * @return exec status
 */
int hw_p1_setup_setnsamples(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t nsamples)
{
	assert(hw);

	if (!nsamples)
		return (-EINVALID);

	hw->set.nsamples = nsamples;

	return (ALL_OK);
}

/**
 * Configure a temperature sensor.
 * @param hw private hw_p1 hardware data
 * @param sensor an allocated sensor structure which will be used as the configuration source for the new sensor
 * @return exec status
 */
int hw_p1_setup_sensor_configure(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_sensor * restrict const sensor)
{
	char * str = NULL;
	sid_t id;

	assert(hw);

	if (!sensor || !sensor->name)
		return (-EUNKNOWN);

	id = sensor->set.sid;
	if (!id || (id > ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	if (hw->Sensors[id-1].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_sid_by_name(hw, sensor->name) > 0)
		return (-EEXISTS);

	// ensure valid type
	if (!hw_lib_sensor_o_to_c(sensor->set.type))
		return (-EINVALID);

	str = strdup(sensor->name);
	if (!str)
		return(-EOOM);

	hw->Sensors[id-1].name = str;
	hw->Sensors[id-1].set.sid = id;
	hw->Sensors[id-1].set.type = sensor->set.type;
	hw->Sensors[id-1].set.offset = sensor->set.offset;
	hw->Sensors[id-1].set.configured = true;

	return (ALL_OK);
}

/**
 * Deconfigure a temperature sensor.
 * @param hw private hw_p1 hardware data
 * @param id the physical id of the sensor to deconfigure (starting from 1)
 * @return exec status
 */
int hw_p1_setup_sensor_deconfigure(struct s_hw_p1_pdata * restrict const hw, const sid_t id)
{
	assert(hw);

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
 * @param hw private hw_p1 hardware data
 * @param id target relay id (starting from 1)
 * @param failstate the state assumed by the hardware relay in standalone failover (controlling software failure)
 * @param name @b unique user-defined name for this relay (string will be copied locally)
 * @return exec status
 * @note sets relay's run.off_since
 */
int hw_p1_setup_relay_request(struct s_hw_p1_pdata * restrict const hw, const rid_t id, const bool failstate, const char * const name)
{
	char * str = NULL;

	assert(hw);

	if (!id || (id > ARRAY_SIZE(hw->Relays)) || !name)
		return (-EINVALID);

	if (hw->Relays[id-1].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_rid_by_name(hw, name) > 0)
		return (-EEXISTS);

	str = strdup(name);
	if (!str)
		return(-EOOM);

	hw->Relays[id-1].name = str;

	// register failover state
	hw->Relays[id-1].set.failstate = failstate;

	hw->Relays[id-1].run.off_since = timekeep_now();	// relay is by definition OFF since "now"

	hw->Relays[id-1].set.configured = true;

	return (ALL_OK);
}

/**
 * Release a hardware relay.
 * @param hw private hw_p1 hardware data
 * Frees and cleans up the target hardware relay.
 * @param id target relay id (starting from 1)
 * @return exec status
 */
int hw_p1_setup_relay_release(struct s_hw_p1_pdata * restrict const hw, const rid_t id)
{
	assert(hw);

	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	if (!hw->Relays[id-1].set.configured)
		return (-ENOTCONFIGURED);

	free(hw->Relays[id-1].name);

	memset(&hw->Relays[id-1], 0x00, sizeof(hw->Relays[id-1]));

	return (ALL_OK);
}

/**
 * Fake destructor.
 * For the sake of API consistency, this simulates a destructor
 * to the "allocated" data in hw_p1_setup_new().
 */
void hw_p1_setup_del(struct s_hw_p1_pdata * restrict const hw)
{
	return;
}
