//
//  filecfg/dump/dhwt_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT file configuration dumping.
 */

#include "dhwt_dump.h"
#include "filecfg_dump.h"
#include "plant/dhwt.h"
#include "plant/pump.h"
#include "plant/valve.h"
#include "lib.h"
#include "inputs.h"
#include "outputs.h"

#include "scheduler.h"

int filecfg_dhwt_params_dump(const struct s_dhwt_params * restrict const params)
{
	if (!params)
		return (-EINVALID);

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || params->limit_chargetime)
		filecfg_dump_tk("limit_chargetime", params->limit_chargetime);
	if (FCD_Exhaustive || params->limit_wintmax)
		filecfg_dump_celsius("limit_wintmax", params->limit_wintmax);
	if (FCD_Exhaustive || params->limit_tmin)
		filecfg_dump_celsius("limit_tmin", params->limit_tmin);
	if (FCD_Exhaustive || params->limit_tmax)
		filecfg_dump_celsius("limit_tmax", params->limit_tmax);

	if (FCD_Exhaustive || params->t_legionella)
		filecfg_dump_celsius("t_legionella", params->t_legionella);
	if (FCD_Exhaustive || params->t_comfort)
		filecfg_dump_celsius("t_comfort", params->t_comfort);
	if (FCD_Exhaustive || params->t_eco)
		filecfg_dump_celsius("t_eco", params->t_eco);
	if (FCD_Exhaustive || params->t_frostfree)
		filecfg_dump_celsius("t_frostfree", params->t_frostfree);

	if (FCD_Exhaustive || params->hysteresis)
		filecfg_dump_deltaK("hysteresis", params->hysteresis);
	if (FCD_Exhaustive || params->temp_inoffset)
		filecfg_dump_deltaK("temp_inoffset", params->temp_inoffset);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_dhwt_dump(const struct s_dhwt * restrict const dhwt)
{
	const char * cpriostr, * fmode;
	int ret = ALL_OK;

	if (!dhwt)
		return (-EINVALID);

	if (!dhwt->set.configured)
		return (-ENOTCONFIGURED);

	switch (dhwt->set.dhwt_cprio) {
		case DHWTP_PARALMAX:
			cpriostr = "paralmax";
			break;
		case DHWTP_PARALDHW:
			cpriostr = "paraldhw";
			break;
		case DHWTP_SLIDMAX:
			cpriostr = "slidmax";
			break;
		case DHWTP_SLIDDHW:
			cpriostr = "sliddhw";
			break;
		case DHWTP_ABSOLUTE:
			cpriostr = "absolute";
			break;
		default:
			cpriostr = "";
			ret = -EMISCONFIGURED;
			break;
	}

	switch (dhwt->set.force_mode) {
		case DHWTF_NEVER:
			fmode = "never";
			break;
		case DHWTF_FIRST:
			fmode = "first";
			break;
		case DHWTF_ALWAYS:
			fmode = "always";
			break;
		default:
			fmode = "";
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_iprintf("dhwt \"%s\" {\n", dhwt->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || dhwt->set.electric_failover)
		filecfg_dump_nodebool("electric_failover", dhwt->set.electric_failover);
	if (FCD_Exhaustive || dhwt->set.anti_legionella)
		filecfg_dump_nodebool("anti_legionella", dhwt->set.anti_legionella);
	if (FCD_Exhaustive || dhwt->set.legionella_recycle)
		filecfg_dump_nodebool("legionella_recycle", dhwt->set.legionella_recycle);
	if (FCD_Exhaustive || dhwt->set.electric_recycle)
		filecfg_dump_nodebool("electric_recycle", dhwt->set.electric_recycle);
	if (FCD_Exhaustive || dhwt->set.prio)
		filecfg_iprintf("prio %hd;\n", dhwt->set.prio);
	if (FCD_Exhaustive || dhwt->set.schedid)
		filecfg_dump_nodestr("schedid", scheduler_get_schedname(dhwt->set.schedid));
	filecfg_dump_nodestr("runmode", filecfg_runmode_str(dhwt->set.runmode));		// mandatory
	filecfg_dump_nodestr("dhwt_cprio", cpriostr);
	filecfg_dump_nodestr("force_mode", fmode);

	if (FCD_Exhaustive || inputs_temperature_name(dhwt->set.tid_bottom))
		filecfg_dump_nodestr("tid_bottom", inputs_temperature_name(dhwt->set.tid_bottom));
	if (FCD_Exhaustive || inputs_temperature_name(dhwt->set.tid_top))
		filecfg_dump_nodestr("tid_top", inputs_temperature_name(dhwt->set.tid_top));
	if (FCD_Exhaustive || inputs_temperature_name(dhwt->set.tid_win))
		filecfg_dump_nodestr("tid_win", inputs_temperature_name(dhwt->set.tid_win));
	if (FCD_Exhaustive || inputs_temperature_name(dhwt->set.tid_wout))
		filecfg_dump_nodestr("tid_wout", inputs_temperature_name(dhwt->set.tid_wout));
	if (FCD_Exhaustive || outputs_relay_name(dhwt->set.rid_selfheater))
		filecfg_dump_nodestr("rid_selfheater", outputs_relay_name(dhwt->set.rid_selfheater));

	filecfg_iprintf("params"); filecfg_dhwt_params_dump(&dhwt->set.params);

	if (FCD_Exhaustive || dhwt->set.p.pump_feed)
		filecfg_dump_nodestr("pump_feed", dhwt->set.p.pump_feed ? dhwt->set.p.pump_feed->name : "");
	if (FCD_Exhaustive || dhwt->set.p.pump_recycle)
		filecfg_dump_nodestr("pump_recycle", dhwt->set.p.pump_recycle ? dhwt->set.p.pump_recycle->name : "");
	if (FCD_Exhaustive || dhwt->set.p.valve_hwisol)
		filecfg_dump_nodestr("valve_hwisol", dhwt->set.p.valve_hwisol ? dhwt->set.p.valve_hwisol->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

