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
#include "inputs.h"

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

	filecfg_dump_nodestr("idle_mode", idlemode);
	filecfg_dump_deltaK("hysteresis", priv->set.hysteresis);			// mandatory
	filecfg_dump_celsius("limit_thardmax", priv->set.limit_thardmax);		// mandatory
	if (FCD_Exhaustive || priv->set.limit_tmax)
		filecfg_dump_celsius("limit_tmax", priv->set.limit_tmax);
	if (FCD_Exhaustive || priv->set.limit_tmin)
		filecfg_dump_celsius("limit_tmin", priv->set.limit_tmin);
	if (FCD_Exhaustive || priv->set.limit_treturnmin)
		filecfg_dump_celsius("limit_treturnmin", priv->set.limit_treturnmin);
	filecfg_dump_celsius("t_freeze", priv->set.t_freeze);				// mandatory
	if (FCD_Exhaustive || priv->set.burner_min_time)
		filecfg_dump_tk("burner_min_time", priv->set.burner_min_time);

	filecfg_dump_nodestr("tid_boiler", inputs_temperature_name(priv->set.tid_boiler));	// mandatory
	if (FCD_Exhaustive || inputs_temperature_name(priv->set.tid_boiler_return))
		filecfg_dump_nodestr("tid_boiler_return", inputs_temperature_name(priv->set.tid_boiler_return));
	filecfg_dump_relid("rid_burner_1", priv->set.rid_burner_1);			// mandatory
	if (FCD_Exhaustive || hardware_relay_name(priv->set.rid_burner_2))
		filecfg_dump_relid("rid_burner_2", priv->set.rid_burner_2);

	if (FCD_Exhaustive || priv->set.p.pump_load)
		filecfg_dump_nodestr("pump_load", priv->set.p.pump_load ? priv->set.p.pump_load->name : "");
	if (FCD_Exhaustive || priv->set.p.valve_ret)
		filecfg_dump_nodestr("valve_ret", priv->set.p.valve_ret ? priv->set.p.valve_ret->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}
