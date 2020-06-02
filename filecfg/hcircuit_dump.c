//
//  filecfg/hcircuit_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit file configuration dumping.
 */

#include "hcircuit_dump.h"
#include "filecfg.h"
#include "hcircuit.h"
#include "pump.h"
#include "valve.h"
#include "hardware.h"
#include "lib.h"

#include "scheduler.h"
#include "models.h"

static int filecfg_hc_tlbilin_dump(const struct s_hcircuit * restrict const circuit)
{
	const struct s_tlaw_bilin20C_priv * restrict priv;

	if (!circuit)
		return (-EINVALID);

	if (HCL_BILINEAR != circuit->set.tlaw)
		return (-EINVALID);

	priv = circuit->tlaw_priv;

	// all params mandatory
	filecfg_iprintf("tout1 %.1f;\n", temp_to_celsius(priv->tout1));
	filecfg_iprintf("twater1 %.1f;\n", temp_to_celsius(priv->twater1));
	filecfg_iprintf("tout2 %.1f;\n", temp_to_celsius(priv->tout2));
	filecfg_iprintf("twater2 %.1f;\n", temp_to_celsius(priv->twater2));
	filecfg_iprintf("nH100 %" PRIdFAST16 ";\n", priv->nH100);

#if 0	// do not print these 'internal' parameters as for now they are not meant to be set externally
	filecfg_iprintf("toutinfl %.1f;\n", temp_to_celsius(priv->toutinfl));
	filecfg_iprintf("twaterinfl %.1f;\n", temp_to_celsius(priv->twaterinfl));
	//filecfg_iprintf("offset %.1f;\n", temp_to_deltaK(priv->offset));	// don't print offset as it's homogenous to internal (meaningless) temperature dimensions
	filecfg_iprintf("slope %.1f;\n", priv->slope);
#endif
	return (ALL_OK);
}

static int filecfg_hcircuit_tlaw_dump(const struct s_hcircuit * restrict const circuit)
{
	const char * tlawname;
	int (*privdump)(const struct s_hcircuit * restrict const);
	int ret = ALL_OK;

	switch (circuit->set.tlaw) {
		case HCL_BILINEAR:
			tlawname = "bilinear";
			privdump = filecfg_hc_tlbilin_dump;
			break;
		case HCL_NONE:
		default:
			tlawname = "";
			privdump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", tlawname);
	filecfg_ilevel_inc();
	if (privdump)
		privdump(circuit);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

int filecfg_hcircuit_params_dump(const struct s_hcircuit_params * restrict const params)
{
	if (!params)
		return (-EINVALID);

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || params->t_comfort)
		filecfg_iprintf("t_comfort %.1f;\n", temp_to_celsius(params->t_comfort));
	if (FCD_Exhaustive || params->t_eco)
		filecfg_iprintf("t_eco %.1f;\n", temp_to_celsius(params->t_eco));
	if (FCD_Exhaustive || params->t_frostfree)
		filecfg_iprintf("t_frostfree %.1f;\n", temp_to_celsius(params->t_frostfree));
	if (FCD_Exhaustive || params->t_offset)
		filecfg_iprintf("t_offset %.1f;\n", temp_to_deltaK(params->t_offset));

	if (FCD_Exhaustive || params->outhoff_comfort)
		filecfg_iprintf("outhoff_comfort %.1f;\n", temp_to_celsius(params->outhoff_comfort));
	if (FCD_Exhaustive || params->outhoff_eco)
		filecfg_iprintf("outhoff_eco %.1f;\n", temp_to_celsius(params->outhoff_eco));
	if (FCD_Exhaustive || params->outhoff_frostfree)
		filecfg_iprintf("outhoff_frostfree %.1f;\n", temp_to_celsius(params->outhoff_frostfree));
	if (FCD_Exhaustive || params->outhoff_hysteresis)
		filecfg_iprintf("outhoff_hysteresis %.1f;\n", temp_to_deltaK(params->outhoff_hysteresis));

	if (FCD_Exhaustive || params->limit_wtmin)
		filecfg_iprintf("limit_wtmin %.1f;\n", temp_to_celsius(params->limit_wtmin));
	if (FCD_Exhaustive || params->limit_wtmax)
		filecfg_iprintf("limit_wtmax %.1f;\n", temp_to_celsius(params->limit_wtmax));

	if (FCD_Exhaustive || params->temp_inoffset)
		filecfg_iprintf("temp_inoffset %.1f;\n", temp_to_deltaK(params->temp_inoffset));

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_hcircuit_dump(const struct s_hcircuit * restrict const circuit)
{
	if (!circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("hcircuit \"%s\" {\n", circuit->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || circuit->set.fast_cooldown)
		filecfg_iprintf("fast_cooldown %s;\n", filecfg_bool_str(circuit->set.fast_cooldown));
	if (FCD_Exhaustive || circuit->set.logging)
		filecfg_iprintf("logging %s;\n", filecfg_bool_str(circuit->set.logging));
	if (FCD_Exhaustive || circuit->set.schedid)
		filecfg_iprintf("schedid \"%s\";\n", scheduler_get_schedname(circuit->set.schedid));
	filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(circuit->set.runmode));		// mandatory
	if (FCD_Exhaustive || circuit->set.ambient_factor)
		filecfg_iprintf("ambient_factor %" PRIdFAST16 ";\n", circuit->set.ambient_factor);
	if (FCD_Exhaustive || circuit->set.wtemp_rorh)
		filecfg_iprintf("wtemp_rorh %.1f;\n", temp_to_deltaK(circuit->set.wtemp_rorh));
	if (FCD_Exhaustive || circuit->set.am_tambient_tK)
		filecfg_iprintf("am_tambient_tK %ld;\n", timekeep_tk_to_sec(circuit->set.am_tambient_tK));
	if (FCD_Exhaustive || circuit->set.tambient_boostdelta)
		filecfg_iprintf("tambient_boostdelta %.1f;\n", temp_to_deltaK(circuit->set.tambient_boostdelta));
	if (FCD_Exhaustive || circuit->set.boost_maxtime)
		filecfg_iprintf("boost_maxtime %ld;\n", timekeep_tk_to_sec(circuit->set.boost_maxtime));

	filecfg_iprintf("tid_outgoing"); filecfg_tempid_dump(circuit->set.tid_outgoing);		// mandatory
	if (FCD_Exhaustive || hardware_sensor_name(circuit->set.tid_return))
		filecfg_iprintf("tid_return"), filecfg_tempid_dump(circuit->set.tid_return);
	if (FCD_Exhaustive || hardware_sensor_name(circuit->set.tid_ambient))
		filecfg_iprintf("tid_ambient"), filecfg_tempid_dump(circuit->set.tid_ambient);

	filecfg_iprintf("params"); filecfg_hcircuit_params_dump(&circuit->set.params);

	filecfg_iprintf("tlaw"); filecfg_hcircuit_tlaw_dump(circuit);					// mandatory

	if (FCD_Exhaustive || circuit->set.p.valve_mix)
		filecfg_iprintf("valve_mix \"%s\";\n", circuit->set.p.valve_mix ? circuit->set.p.valve_mix->name : "");
	if (FCD_Exhaustive || circuit->set.p.pump_feed)
		filecfg_iprintf("pump_feed \"%s\";\n", circuit->set.p.pump_feed ? circuit->set.p.pump_feed->name : "");
	if (FCD_Exhaustive || circuit->set.p.bmodel)
		filecfg_iprintf("bmodel \"%s\";\n", circuit->set.p.bmodel ? circuit->set.p.bmodel->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
