//
//  filecfg/dump/outputs_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Outputs subsystem file configuration dumping.
 */

#include "outputs_dump.h"
#include "filecfg_dump.h"
#include "relay_dump.h"
#include "io/outputs.h"

extern struct s_outputs Outputs;

void filecfg_outputs_dump(void)
{
	unsigned int id;

	filecfg_iprintf("outputs {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("relays {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < Outputs.relays.last; id++)
		filecfg_relay_dump(&Outputs.relays.all[id]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}
