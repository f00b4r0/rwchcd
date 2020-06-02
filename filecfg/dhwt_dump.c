//
//  filecfg/dhwt_dump.c
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
#include "filecfg.h"
#include "dhwt.h"
#include "pump.h"
#include "valve.h"
#include "hardware.h"
#include "lib.h"

#include "scheduler.h"

int filecfg_dhwt_params_dump(const struct s_dhwt_params * restrict const params)
{
	if (!params)
		return (-EINVALID);

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || params->limit_chargetime)
		filecfg_iprintf("limit_chargetime %ld;\n", timekeep_tk_to_sec(params->limit_chargetime));
	if (FCD_Exhaustive || params->limit_wintmax)
		filecfg_iprintf("limit_wintmax %.1f;\n", temp_to_celsius(params->limit_wintmax));
	if (FCD_Exhaustive || params->limit_tmin)
		filecfg_iprintf("limit_tmin %.1f;\n", temp_to_celsius(params->limit_tmin));
	if (FCD_Exhaustive || params->limit_tmax)
		filecfg_iprintf("limit_tmax %.1f;\n", temp_to_celsius(params->limit_tmax));

	if (FCD_Exhaustive || params->t_legionella)
		filecfg_iprintf("t_legionella %.1f;\n", temp_to_celsius(params->t_legionella));
	if (FCD_Exhaustive || params->t_comfort)
		filecfg_iprintf("t_comfort %.1f;\n", temp_to_celsius(params->t_comfort));
	if (FCD_Exhaustive || params->t_eco)
		filecfg_iprintf("t_eco %.1f;\n", temp_to_celsius(params->t_eco));
	if (FCD_Exhaustive || params->t_frostfree)
		filecfg_iprintf("t_frostfree %.1f;\n", temp_to_celsius(params->t_frostfree));

	if (FCD_Exhaustive || params->hysteresis)
		filecfg_iprintf("hysteresis %.1f;\n", temp_to_deltaK(params->hysteresis));
	if (FCD_Exhaustive || params->temp_inoffset)
		filecfg_iprintf("temp_inoffset %.1f;\n", temp_to_deltaK(params->temp_inoffset));

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
		filecfg_iprintf("electric_failover %s;\n", filecfg_bool_str(dhwt->set.electric_failover));
	if (FCD_Exhaustive || dhwt->set.anti_legionella)
		filecfg_iprintf("anti_legionella %s;\n", filecfg_bool_str(dhwt->set.anti_legionella));
	if (FCD_Exhaustive || dhwt->set.legionella_recycle)
		filecfg_iprintf("legionella_recycle %s;\n", filecfg_bool_str(dhwt->set.legionella_recycle));
	if (FCD_Exhaustive || dhwt->set.electric_recycle)
		filecfg_iprintf("electric_recycle %s;\n", filecfg_bool_str(dhwt->set.electric_recycle));
	if (FCD_Exhaustive || dhwt->set.prio)
		filecfg_iprintf("prio %hd;\n", dhwt->set.prio);
	if (FCD_Exhaustive || dhwt->set.schedid)
		filecfg_iprintf("schedid \"%s\";\n", scheduler_get_schedname(dhwt->set.schedid));
	filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(dhwt->set.runmode));		// mandatory
	filecfg_iprintf("dhwt_cprio \"%s\";\n", cpriostr);
	filecfg_iprintf("force_mode \"%s\";\n", fmode);

	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_bottom))
		filecfg_iprintf("tid_bottom"), filecfg_tempid_dump(dhwt->set.tid_bottom);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_top))
		filecfg_iprintf("tid_top"), filecfg_tempid_dump(dhwt->set.tid_top);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_win))
		filecfg_iprintf("tid_win"), filecfg_tempid_dump(dhwt->set.tid_win);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_wout))
		filecfg_iprintf("tid_wout"), filecfg_tempid_dump(dhwt->set.tid_wout);
	if (FCD_Exhaustive || hardware_relay_name(dhwt->set.rid_selfheater))
		filecfg_iprintf("rid_selfheater"), filecfg_relid_dump(dhwt->set.rid_selfheater);

	filecfg_iprintf("params"); filecfg_dhwt_params_dump(&dhwt->set.params);

	if (FCD_Exhaustive || dhwt->set.p.pump_feed)
		filecfg_iprintf("pump_feed \"%s\";\n", dhwt->set.p.pump_feed ? dhwt->set.p.pump_feed->name : "");
	if (FCD_Exhaustive || dhwt->set.p.pump_recycle)
		filecfg_iprintf("pump_recycle \"%s\";\n", dhwt->set.p.pump_recycle ? dhwt->set.p.pump_recycle->name : "");
	if (FCD_Exhaustive || dhwt->set.p.valve_hwisol)
		filecfg_iprintf("valve_hwisol \"%s\";\n", dhwt->set.p.valve_hwisol ? dhwt->set.p.valve_hwisol->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

