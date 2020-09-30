//
//  filecfg/backends_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration dumping.
 */

#include "backends_dump.h"
#include "filecfg_dump.h"
#include "hw_backends.h"

extern struct s_hw_backends HW_backends;

void filecfg_backends_dump()
{
	unsigned int id;

	filecfg_iprintf("backends {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < HW_backends.last; id++) {
		filecfg_iprintf("backend \"%s\" {\n", HW_backends.all[id].name);
		filecfg_ilevel_inc();
		if (HW_backends.all[id].cb->filecfg_dump)
			HW_backends.all[id].cb->filecfg_dump(HW_backends.all[id].priv);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}
