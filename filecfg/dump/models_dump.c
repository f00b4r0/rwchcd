//
//  filecfg/dump/models_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models subsystem file configuration dumping.
 */

#include "models_dump.h"
#include "models.h"
#include "filecfg_dump.h"
#include "lib.h"

extern struct s_models Models;

static int filecfg_bmodel_dump(const struct s_bmodel * restrict const bmodel)
{
	if (!bmodel)
		return (-EINVALID);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("bmodel \"%s\" {\n", bmodel->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || bmodel->set.log)
		filecfg_dump_nodebool("log", bmodel->set.log);
	if (FCD_Exhaustive || bmodel->set.limit_tsummer)
		filecfg_dump_celsius("limit_tsummer", bmodel->set.limit_tsummer);
	if (FCD_Exhaustive || bmodel->set.limit_tfrost)
		filecfg_dump_celsius("limit_tfrost", bmodel->set.limit_tfrost);
	filecfg_dump_tk("tau", bmodel->set.tau);			// mandatory
	filecfg_dump_nodestr("tid_outdoor", inputs_temperature_name(bmodel->set.tid_outdoor));	// mandatory

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_models_dump(void)
{
	modid_t id;
	const struct s_bmodel * bmodel;

	filecfg_iprintf("models {\n");
	filecfg_ilevel_inc();
	for (id = 0; id < Models.bmodels.last; id++) {
		bmodel = &Models.bmodels.all[id];
		if (!bmodel->set.configured)
			continue;
		filecfg_bmodel_dump(bmodel);
	}
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

