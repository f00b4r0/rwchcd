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
#include <stdlib.h>

#include "timekeep.h"
#include "hw_p1_setup.h"
#include "hw_p1_lcd.h"

#define SPICLOCK	1000000		///< SPI clock 1MHz
#define SPICHAN		0		///< RaspberryPi SPI channel 0

/**
 * Allocate & initialize local HW P1 data.
 * @return pointer to HW P1 private data
 */
void * hw_p1_setup_new(void)
{
	struct s_hw_p1_pdata * hw;

	hw = calloc(1, sizeof(*hw));
	if (!hw)
		return (NULL);

	hw->spi.set.chan = SPICHAN;
	hw->spi.set.clock = SPICLOCK;

	hw_p1_lcd_init(&hw->lcd);

	return (hw);
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
 * Configure a temperature sensor.
 * @param hw private hw_p1 hardware data
 * @param sensor an allocated sensor structure which will be used as the configuration source for the new sensor
 * @return exec status
 */
int hw_p1_setup_sensor_configure(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_p1_sensor * restrict const sensor)
{
	uint_fast8_t id, i;
	char * str;

	assert(hw);

	if (!sensor || !sensor->name)
		return (-EUNKNOWN);

	id = hw->run.nsensors;
	if (id >= ARRAY_SIZE(hw->Sensors))
		return (-EINVALID);

	// ensure unique name
	if (hw_p1_sid_by_name(hw, sensor->name) > 0)
		return (-EEXISTS);

	// ensure valid type
	if (!sensor->set.type)
		return (-EINVALID);

	if (sensor->set.channel > ARRAY_SIZE(hw->sensors))
		return (-EINVALID);

	// check for channel collision
	for (i = 0; i < hw->run.nsensors; i++) {
		if (sensor->set.channel == hw->Sensors[i].set.channel)
			return (-EEXISTS);
	}

	str = strdup(sensor->name);
	if (!str)
		return(-EOOM);

	hw->Sensors[id].name = str;
	hw->Sensors[id].set.channel = sensor->set.channel;
	hw->Sensors[id].set.type = sensor->set.type;
	hw->Sensors[id].set.offset = sensor->set.offset;
	hw->Sensors[id].set.configured = true;

	hw->run.nsensors++;

	return (ALL_OK);
}

/**
 * Deconfigure a temperature sensor.
 * @param hw private hw_p1 hardware data
 * @param id the id of the sensor to deconfigure (starting from 0)
 * @return exec status
 */
int hw_p1_setup_sensor_deconfigure(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t id)
{
	assert(hw);

	if ((id >= ARRAY_SIZE(hw->Sensors)))
		return (-EINVALID);

	if (!hw->Sensors[id].set.configured)
		return (-ENOTCONFIGURED);

	freeconst(hw->Sensors[id].name);
	memset(&hw->Sensors[id], 0x00, sizeof(hw->Sensors[id]));

	return (ALL_OK);
}

/**
 * Request a hardware relay.
 * Ensures that the desired hardware relay is available and grabs it.
 * @param hw private hw_p1 hardware data
 * @param relay an allocated relay structure which will be used as the configuration source for the new relay
 * @return exec status
 */
int hw_p1_setup_relay_request(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_p1_relay * restrict const relay)
{
	char * str;
	uint_fast8_t id;

	assert(hw);

	if (!relay || !relay->name)
		return (-EUNKNOWN);

	id = relay->set.channel;
	if (!id || (id > ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	id--;	// relay array indexes from 0
	if (hw->Relays[id].set.configured)
		return (-EEXISTS);

	// ensure unique name
	if (hw_p1_rid_by_name(hw, relay->name) > 0)
		return (-EEXISTS);

	str = strdup(relay->name);
	if (!str)
		return(-EOOM);

	hw->Relays[id].name = str;
	hw->Relays[id].set.failstate = relay->set.failstate;	// register failover state
	hw->Relays[id].set.channel = relay->set.channel;
	hw->Relays[id].run.state_since = timekeep_now();	// relay is by definition OFF since "now"
	hw->Relays[id].set.configured = true;

	return (ALL_OK);
}

/**
 * Release a hardware relay.
 * @param hw private hw_p1 hardware data
 * Frees and cleans up the target hardware relay.
 * @param id target relay id (starting from 0)
 * @return exec status
 */
int hw_p1_setup_relay_release(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t id)
{
	assert(hw);

	if ((id >= ARRAY_SIZE(hw->Relays)))
		return (-EINVALID);

	if (!hw->Relays[id].set.configured)
		return (-ENOTCONFIGURED);

	freeconst(hw->Relays[id].name);

	memset(&hw->Relays[id], 0x00, sizeof(hw->Relays[id]));

	return (ALL_OK);
}

/**
 * HW P1 destructor.
 * Frees data allocated in hw_p1_setup_new().
 */
void hw_p1_setup_del(struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast8_t i;

	// cleanup all resources
	for (i = 0; i < ARRAY_SIZE(hw->Relays); i++)
		hw_p1_setup_relay_release(hw, i);

	// deconfigure all sensors
	for (i = 0; i < ARRAY_SIZE(hw->Sensors); i++)
		hw_p1_setup_sensor_deconfigure(hw, i);

	hw_p1_lcd_exit(&hw->lcd);

	free(hw->availsysmodes);
	free(hw);
}
