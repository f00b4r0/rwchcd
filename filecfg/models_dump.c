//
//  filecfg/models_dump.c
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
#include "filecfg.h"

extern struct s_models Models;

static int filecfg_bmodel_dump(const struct s_bmodel * restrict const bmodel)
{
	if (!bmodel)
		return (-EINVALID);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("bmodel \"%s\" {\n", bmodel->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || bmodel->set.logging)
		filecfg_iprintf("logging %s;\n", filecfg_bool_str(bmodel->set.logging));
	filecfg_iprintf("tau %ld;\n", timekeep_tk_to_sec(bmodel->set.tau));						// mandatory
	filecfg_iprintf("tid_outdoor"); filecfg_tempid_dump(bmodel->set.tid_outdoor);		// mandatory

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_models_dump(void)
{
	struct s_bmodel_l * restrict bmodelelmt;

	filecfg_iprintf("models {\n");
	filecfg_ilevel_inc();
	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		filecfg_bmodel_dump(bmodelelmt->bmodel);
	}
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

