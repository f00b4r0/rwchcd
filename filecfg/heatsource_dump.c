//
//  filecfg/heatsource_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource file configuration dumping.
 */

#include "heatsource_dump.h"
#include "heatsource.h"
#include "filecfg_dump.h"
#include "scheduler.h"

#include "boiler_dump.h"

static int filecfg_heatsource_type_dump(const struct s_heatsource * restrict const heat)
{
	const char * typename;
	int (*privdump)(const struct s_heatsource * restrict const);
	int ret = ALL_OK;

	switch (heat->set.type) {
		case HS_BOILER:
			typename = "boiler";
			privdump = filecfg_boiler_hs_dump;
			break;
		case HS_NONE:
		case HS_UNKNOWN:
		default:
			ret = -EINVALID;
			typename = "";
			privdump = NULL;
			break;
	}

	filecfg_printf(" \"%s\"", typename);
	if (privdump)
		privdump(heat);

	return (ret);
}

int filecfg_heatsource_dump(const struct s_heatsource * restrict const heat)
{
	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("heatsource \"%s\" {\n", heat->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || heat->set.schedid)
		filecfg_iprintf("schedid \"%s\";\n", scheduler_get_schedname(heat->set.schedid));
	filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(heat->set.runmode));	// mandatory
	filecfg_iprintf("type"); filecfg_heatsource_type_dump(heat);			// mandatory
	if (FCD_Exhaustive || heat->set.prio)
		filecfg_iprintf("prio %hd;\n", heat->set.prio);
	if (FCD_Exhaustive || heat->set.consumer_sdelay)
		filecfg_iprintf("consumer_sdelay %ld;\n", timekeep_tk_to_sec(heat->set.consumer_sdelay));

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
