//
//  filecfg/boiler_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler heatsource file configuration dumping.
 */

#include "boiler_dump.h"
#include "boiler.h"
#include "filecfg_dump.h"

#include "pump.h"
#include "valve.h"
#include "hardware.h"

int filecfg_boiler_hs_dump(const struct s_heatsource * restrict const heat)
{
	const char * idlemode;
	const struct s_boiler_priv * restrict priv;
	int ret = ALL_OK;

	if (!heat)
		return (-EINVALID);

	if (HS_BOILER != heat->set.type)
		return (-EINVALID);

	priv = heat->priv;

	switch (priv->set.idle_mode) {
		case IDLE_NEVER:
			idlemode = "never";
			break;
		case IDLE_ALWAYS:
			idlemode = "always";
			break;
		case IDLE_FROSTONLY:
			idlemode = "frostonly";
			break;
		default:
			idlemode = "";
			ret = -EMISCONFIGURED;
	}

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("idle_mode \"%s\";\n", idlemode);
	filecfg_iprintf("hysteresis %.1f;\n", temp_to_deltaK(priv->set.hysteresis));				// mandatory
	filecfg_iprintf("limit_thardmax %.1f;\n", temp_to_celsius(priv->set.limit_thardmax));			// mandatory
	if (FCD_Exhaustive || priv->set.limit_tmax)
		filecfg_iprintf("limit_tmax %.1f;\n", temp_to_celsius(priv->set.limit_tmax));
	if (FCD_Exhaustive || priv->set.limit_tmin)
		filecfg_iprintf("limit_tmin %.1f;\n", temp_to_celsius(priv->set.limit_tmin));
	if (FCD_Exhaustive || priv->set.limit_treturnmin)
		filecfg_iprintf("limit_treturnmin %.1f;\n", temp_to_celsius(priv->set.limit_treturnmin));
	filecfg_iprintf("t_freeze %.1f;\n", temp_to_celsius(priv->set.t_freeze));				// mandatory
	if (FCD_Exhaustive || priv->set.burner_min_time)
		filecfg_iprintf("burner_min_time %ld;\n", timekeep_tk_to_sec(priv->set.burner_min_time));

	filecfg_iprintf("tid_boiler"); filecfg_tempid_dump(priv->set.tid_boiler);				// mandatory
	if (FCD_Exhaustive || hardware_sensor_name(priv->set.tid_boiler_return))
		filecfg_iprintf("tid_boiler_return"), filecfg_tempid_dump(priv->set.tid_boiler_return);
	filecfg_iprintf("rid_burner_1"); filecfg_relid_dump(priv->set.rid_burner_1);				// mandatory
	if (FCD_Exhaustive || hardware_relay_name(priv->set.rid_burner_2))
		filecfg_iprintf("rid_burner_2"), filecfg_relid_dump(priv->set.rid_burner_2);

	if (FCD_Exhaustive || priv->set.p.pump_load)
		filecfg_iprintf("pump_load \"%s\";\n", priv->set.p.pump_load ? priv->set.p.pump_load->name : "");
	if (FCD_Exhaustive || priv->set.p.valve_ret)
		filecfg_iprintf("valve_ret \"%s\";\n", priv->set.p.valve_ret ? priv->set.p.valve_ret->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}
