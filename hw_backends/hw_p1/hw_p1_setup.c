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

#include <string.h>	// memset
#include <assert.h>

#include "hw_p1_setup.h"

#define SPICLOCK	1000000		///< SPI clock 1MHz
#define SPICHAN		0		///< RaspberryPi SPI channel 0

static struct s_hw_p1_pdata Hardware;	///< Prototype 1 private data

/**
 * Initialize local data.
 * Cannot fail.
 * @return #Hardware
 */
void * hw_p1_setup_new(void)
{
	memset(&Hardware, 0x0, sizeof(Hardware));
	
	Hardware.spi.set.chan = SPICHAN;
	Hardware.spi.set.clock = SPICLOCK;

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
	sid_t id;

	assert(hw);

	if (!sensor || !sensor->name)
		return (-EUNKNOWN);

	id = hw_lib_sensor_cfg_get_sid(sensor);
	if (!id || (id > ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	id--;	// sensor array indexes from 0
	if (hw_lib_sensor_is_configured(&hw->Sensors[id]))
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_sid_by_name(hw, hw_lib_sensor_get_name(sensor)) > 0)
		return (-EEXISTS);

	return (hw_lib_sensor_setup_copy(&hw->Sensors[id], sensor));
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

	if (!hw_lib_sensor_is_configured(&hw->Sensors[id-1]))
		return (-ENOTCONFIGURED);

	hw_lib_sensor_discard(&hw->Sensors[id-1]);

	return (ALL_OK);
}

/**
 * Request a hardware relay.
 * Ensures that the desired hardware relay is available and grabs it.
 * @param hw private hw_p1 hardware data
 * @param relay an allocated relay structure which will be used as the configuration source for the new relay
 * @return exec status
 */
int hw_p1_setup_relay_request(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_relay * restrict const relay)
{
	rid_t id;

	assert(hw);

	if (!relay || !relay->name)
		return (-EUNKNOWN);

	id = hw_lib_relay_cfg_get_rid(relay);
	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	id--;	// relay array indexes from 0
	if (hw_lib_relay_is_configured(&hw->Relays[id-1]))
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_rid_by_name(hw, hw_lib_relay_get_name(relay)) > 0)
		return (-EEXISTS);

	return (hw_lib_relay_setup_copy(&hw->Relays[id], relay));
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

	if (!hw_lib_relay_is_configured(&hw->Relays[id-1]))
		return (-ENOTCONFIGURED);

	hw_lib_relay_discard(&hw->Relays[id-1]);

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
