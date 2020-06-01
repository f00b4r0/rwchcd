//
//  filecfg/pump_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration dumping.
 */

#include "pump_dump.h"
#include "filecfg.h"

int filecfg_pump_dump(const struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("pump \"%s\" {\n", pump->name);
	filecfg_ilevel_inc();
	if (FCD_Exhaustive || pump->set.cooldown_time)
		filecfg_iprintf("cooldown_time %ld;\n", timekeep_tk_to_sec(pump->set.cooldown_time));
	filecfg_iprintf("rid_pump"); filecfg_relid_dump(pump->set.rid_pump);	// mandatory
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
