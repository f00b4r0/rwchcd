//
//  hw_p1_filecfg.c
//  rwchcd
//
//  (C) 2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 file configuration implementation.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "lib.h"
#include "filecfg.h"
#include "hw_p1.h"
#include "hw_p1_filecfg.h"

static void config_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	assert(hw);

	filecfg_iprintf("type \"hw_p1\" {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("nsamples %" PRIdFAST8 ";\n", hw->set.nsamples);
	filecfg_iprintf("nsensors %d;\n", hw->settings.nsensors);
	filecfg_iprintf("lcdbl %d;\n", hw->settings.lcdblpct);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

static void sensors_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	const struct s_hw_p1_sensor * sensor;
	const char * type;
	int_fast8_t id;

	assert(hw);

	if (!FCD_Exhaustive && !hw->settings.nsensors)
		return;

	filecfg_iprintf("sensors {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < hw->settings.nsensors; id++) {
		sensor = &hw->Sensors[id];
		if (!sensor->set.configured)
			continue;

		switch (sensor->set.type) {
			case ST_PT1000:
				type = "PT1000";
				break;
			case ST_NI1000:
				type = "NI1000";
				break;
			default:
				type = "";
				break;
		}

		filecfg_iprintf("sensor \"%s\" {\n", sensor->name);
		filecfg_ilevel_inc();
		filecfg_iprintf("id %d;\n", id+1);
		filecfg_iprintf("type \"%s\";\n", type);
		if (FCD_Exhaustive || sensor->set.offset)
			filecfg_iprintf("offset %.1f;\n", temp_to_deltaK(sensor->set.offset));
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

static void relays_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	const struct s_hw_p1_relay * relay;
	uint_fast8_t id;

	assert(hw);

	filecfg_iprintf("relays {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < ARRAY_SIZE(hw->Relays); id++) {
		relay = &hw->Relays[id];
		if (!relay->set.configured)
			continue;

		filecfg_iprintf("relay \"%s\" {\n", relay->name);
		filecfg_ilevel_inc();
		filecfg_iprintf("id %d;\n", id+1);
		filecfg_iprintf("failstate %s;\n", relay->set.failstate ? "on" : "off");
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump backend configuration to file.
 * @param priv private hardware data
 * @param file target file to dump configuration to
 * @param il indentation level
 * @return exec status
 */
int hw_p1_filecfg_dump(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;

	if (!hw)
		return (-EINVALID);

	config_dump(hw);
	sensors_dump(hw);
	relays_dump(hw);

	return (ALL_OK);
}
