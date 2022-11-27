//
//  filecfg/dump/pump_dump.c
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
#include "filecfg_dump.h"
#include "io/outputs.h"
#include "plant/pump_priv.h"

int filecfg_pump_dump(const struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("pump \"%s\" {\n", pump->name);
	filecfg_ilevel_inc();
	if (FCD_Exhaustive || pump->set.shared)
		filecfg_dump_nodebool("shared", pump->set.shared);
	filecfg_dump_nodestr("rid_pump", outputs_relay_name(pump->set.rid_pump));	// mandatory
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
