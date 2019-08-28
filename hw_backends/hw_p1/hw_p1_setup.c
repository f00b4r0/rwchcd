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
int hw_p1_setup_setnsensors(struct s_hw_p1_pdata * restrict const hw, const sid_t lastid)
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

	id--;	// sensor array indexes from 0
	if (hw->Sensors[id].set.configured)
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

	hw->Sensors[id].name = str;
	hw->Sensors[id].set.sid = sensor->set.sid;
	hw->Sensors[id].set.type = sensor->set.type;
	hw->Sensors[id].set.offset = sensor->set.offset;
	hw->Sensors[id].set.configured = true;

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
 * @param relay an allocated relay structure which will be used as the configuration source for the new relay
 * @return exec status
 * @note sets relay's run.off_since
 */
int hw_p1_setup_relay_request(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_relay * restrict const relay)
{
	char * str = NULL;
	rid_t id;

	assert(hw);

	if (!relay || !relay->name)
		return (-EUNKNOWN);

	id = relay->set.rid;
	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	id--;	// relay array indexes from 0
	if (hw->Relays[id-1].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_rid_by_name(hw, relay->name) > 0)
		return (-EEXISTS);

	str = strdup(relay->name);
	if (!str)
		return(-EOOM);

	hw->Relays[id].name = str;

	// register failover state
	hw->Relays[id].set.failstate = relay->set.failstate;
	hw->Relays[id].set.rid = relay->set.rid;

	hw->Relays[id].run.off_since = timekeep_now();	// relay is by definition OFF since "now"

	hw->Relays[id].set.configured = true;

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
