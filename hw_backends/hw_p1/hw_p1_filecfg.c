//
//  hw_p1_filecfg.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
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

static void config_dump(const struct s_hw_p1_pdata * restrict const hw, FILE * restrict file, unsigned int il)
{
	assert(hw && file);

	tfprintf(file, il, "type \"hw_p1\" {\n");
	il++;

	tfprintf(file, il, "nsamples %" PRIdFAST8 ";\n", hw->set.nsamples);
	tfprintf(file, il, "nsensors %d;\n", hw->settings.nsensors);
	tfprintf(file, il, "lcdbl %d;\n", hw->settings.lcdblpct);

	il--;
	tfprintf(file, il, "};\n");
}

static void sensors_dump(const struct s_hw_p1_pdata * restrict const hw, FILE * restrict file, unsigned int il)
{
	const struct s_hw_p1_sensor * sensor;
	const char * type;
	int_fast8_t id;

	assert(hw && file);

	if (!FCD_Exhaustive && !hw->settings.nsensors)
		return;

	tfprintf(file, il, "sensors {\n");
	il++;

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

		tfprintf(file, il, "sensor \"%s\" {\n", sensor->name);
		il++;
		tfprintf(file, il, "id %d;\n", id+1);
		tfprintf(file, il, "type \"%s\";\n", type);
		if (FCD_Exhaustive || sensor->set.offset)
			tfprintf(file, il, "offset %.1f;\n", temp_to_deltaK(sensor->set.offset));
		il--;
		tfprintf(file, il, "};\n");
	}

	il--;
	tfprintf(file, il, "};\n");
}

static void relays_dump(const struct s_hw_p1_pdata * restrict const hw, FILE * restrict file, unsigned int il)
{
	const struct s_hw_p1_relay * relay;
	uint_fast8_t id;

	assert(hw && file);

	tfprintf(file, il, "relays {\n");
	il++;

	for (id = 0; id < ARRAY_SIZE(hw->Relays); id++) {
		relay = &hw->Relays[id];
		if (!relay->set.configured)
			continue;

		tfprintf(file, il, "relay \"%s\" {\n", relay->name);
		il++;
		tfprintf(file, il, "id %d;\n", id+1);
		tfprintf(file, il, "failstate %s;\n", relay->set.failstate ? "on" : "off");
		il--;
		tfprintf(file, il, "};\n");
	}

	il--;
	tfprintf(file, il, "};\n");
}

/**
 * Dump backend configuration to file.
 * @param priv private hardware data
 * @param file target file to dump configuration to
 * @param il indentation level
 * @return exec status
 */
int hw_p1_filecfg_dump(void * priv, FILE * restrict file, unsigned int il)
{
	struct s_hw_p1_pdata * const hw = priv;

	if (!hw || !file)
		return (-EINVALID);

	config_dump(hw, file, il);
	sensors_dump(hw, file, il);
	relays_dump(hw, file, il);

	return (ALL_OK);
}
