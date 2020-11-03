//
//  filecfg/dump/plant_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant file configuration dumping.
 */

#include "plant_dump.h"
#include "filecfg_dump.h"
#include "rwchcd.h"
#include "plant/plant.h"

#include "pump_dump.h"
#include "valve_dump.h"
#include "heatsource_dump.h"
#include "hcircuit_dump.h"
#include "dhwt_dump.h"

int filecfg_plant_dump(const struct s_plant * restrict const plant)
{
	struct s_heatsource_l * heatsl;
	struct s_dhw_tank_l * dhwtl;
	plid_t id;

	if (!plant)
		return (-EINVALID);

	if (!plant->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("plant {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("config {\n");
	filecfg_ilevel_inc();
	if (FCD_Exhaustive || plant->set.summer_maintenance)
		filecfg_dump_nodebool("summer_maintenance", plant->set.summer_maintenance);
	if (FCD_Exhaustive || plant->set.sleeping_delay)
		filecfg_dump_tk("sleeping_delay", plant->set.sleeping_delay);
	if (FCD_Exhaustive || plant->set.summer_run_interval)
		filecfg_dump_tk("summer_run_interval", plant->set.summer_run_interval);
	if (FCD_Exhaustive || plant->set.summer_run_duration)
		filecfg_dump_tk("summer_run_duration", plant->set.summer_run_duration);
	filecfg_iprintf("def_hcircuit"); filecfg_hcircuit_params_dump(&plant->pdata.set.def_hcircuit);
	filecfg_iprintf("def_dhwt"); filecfg_dhwt_params_dump(&plant->pdata.set.def_dhwt);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");		// config

	if (FCD_Exhaustive || plant->pumps.last) {
		filecfg_iprintf("pumps {\n");
		filecfg_ilevel_inc();
		for (id = 0; id < plant->pumps.last; id++)
			filecfg_pump_dump(&plant->pumps.all[id]);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// pumps
	}

	if (FCD_Exhaustive || plant->valves.last) {
		filecfg_iprintf("valves {\n");
		filecfg_ilevel_inc();
		for (id = 0; id < plant->valves.last; id++)
			filecfg_valve_dump(&plant->valves.all[id]);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// valves
	}

	if (FCD_Exhaustive || plant->heats_head) {
		filecfg_iprintf("heatsources {\n");
		filecfg_ilevel_inc();
		for (heatsl = plant->heats_head; heatsl != NULL; heatsl = heatsl->next)
			filecfg_heatsource_dump(heatsl->heats);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// heatsources
	}

	if (FCD_Exhaustive || plant->hcircuits.last) {
		filecfg_iprintf("hcircuits {\n");
		filecfg_ilevel_inc();
		for (id = 0; id < plant->hcircuits.last; id++)
			filecfg_hcircuit_dump(&plant->hcircuits.all[id]);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// heating_circuits
	}

	if (FCD_Exhaustive || plant->dhwt_head) {
		filecfg_iprintf("dhwts {\n");
		filecfg_ilevel_inc();
		for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next)
			filecfg_dhwt_dump(dhwtl->dhwt);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// dhwts
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");	// plant

	return (ALL_OK);
}
