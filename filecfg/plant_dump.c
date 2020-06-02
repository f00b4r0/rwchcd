//
//  filecfg/plant_dump.c
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
#include "filecfg.h"
#include "rwchcd.h"
#include "plant.h"

#include "pump_dump.h"
#include "valve_dump.h"
#include "heatsource_dump.h"
#include "hcircuit_dump.h"
#include "dhwt_dump.h"

int filecfg_plant_dump(const struct s_plant * restrict const plant)
{
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heatsource_l * heatsl;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;

	if (!plant)
		return (-EINVALID);

	if (!plant->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("plant {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("config {\n");
	filecfg_ilevel_inc();
	if (FCD_Exhaustive || plant->set.summer_maintenance)
		filecfg_iprintf("summer_maintenance %s;\n", filecfg_bool_str(plant->set.summer_maintenance));
	if (FCD_Exhaustive || plant->set.sleeping_delay)
		filecfg_iprintf("sleeping_delay %ld;\n", timekeep_tk_to_sec(plant->set.sleeping_delay));
	if (FCD_Exhaustive || plant->set.summer_run_interval)
		filecfg_iprintf("summer_run_interval %ld;\n", timekeep_tk_to_sec(plant->set.summer_run_interval));
	if (FCD_Exhaustive || plant->set.summer_run_duration)
		filecfg_iprintf("summer_run_duration %ld;\n", timekeep_tk_to_sec(plant->set.summer_run_duration));
	filecfg_iprintf("def_hcircuit"); filecfg_hcircuit_params_dump(&plant->set.def_hcircuit);
	filecfg_iprintf("def_dhwt"); filecfg_dhwt_params_dump(&plant->set.def_dhwt);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");		// config

	if (FCD_Exhaustive || plant->pump_head) {
		filecfg_iprintf("pumps {\n");
		filecfg_ilevel_inc();
		for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next)
			filecfg_pump_dump(pumpl->pump);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");	// pumps
	}

	if (FCD_Exhaustive || plant->valve_head) {
		filecfg_iprintf("valves {\n");
		filecfg_ilevel_inc();
		for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next)
			filecfg_valve_dump(valvel->valve);
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

	if (FCD_Exhaustive || plant->circuit_head) {
		filecfg_iprintf("hcircuits {\n");
		filecfg_ilevel_inc();
		for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next)
			filecfg_hcircuit_dump(circuitl->circuit);
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
