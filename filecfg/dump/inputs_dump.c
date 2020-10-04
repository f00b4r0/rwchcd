//
//  filecfg/dump/inputs_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Inputs subsystem file configuration dumping.
 */

#include "inputs_dump.h"
#include "filecfg_dump.h"
#include "temperature_dump.h"
#include "io/inputs.h"

extern struct s_inputs Inputs;

void filecfg_inputs_dump(void)
{
	unsigned int id;

	filecfg_iprintf("inputs {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("temperatures {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < Inputs.temps.last; id++)
		filecfg_temperature_dump(&Inputs.temps.all[id]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}
