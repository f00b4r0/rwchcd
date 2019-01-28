//
//  filecfg.c
//  rwchcd
//
//  (C) 2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File configuration dump interface implementation.
 */

#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>

#include "rwchcd.h"
#include "lib.h"
#include "hw_backends.h"
#include "hardware.h"
#include "pump.h"
#include "valve.h"
#include "boiler.h"
#include "heatsource.h"
#include "dhwt.h"
#include "hcircuit.h"
#include "models.h"
#include "config.h"
#include "runtime.h"
#include "plant.h"
#include "filecfg.h"

#define FILECONFIG_NAME		"dumpcfg.txt"	///< target file for configuration dump

bool FCD_Exhaustive = false;

/**
 * Programmatically indent with tabs.
 * @param level desired indentation level
 * @return a string containing the required number of '\t'
 */
const char * filecfg_tabs(const unsigned int level)
{
	const char * const indents[] = {
		"",
		"\t",
		"\t\t",
		"\t\t\t",
		"\t\t\t\t",
		"\t\t\t\t\t",
		"\t\t\t\t\t\t",
		"\t\t\t\t\t\t\t",
	};

	if (level >= ARRAY_SIZE(indents))
		return ("");

	return (indents[level]);
}

static void filecfg_backends_dump(FILE * restrict file, unsigned int il)
{
	unsigned int id;

	tfprintf(file, il, "backends {\n");
	il++;

	for (id = 0; (id < ARRAY_SIZE(HW_backends) && HW_backends[id]); id++) {
		tfprintf(file, il, "backend \"%s\" {\n", HW_backends[id]->name);
		il++;
		if (HW_backends[id]->cb->filecfg_dump)
			HW_backends[id]->cb->filecfg_dump(HW_backends[id]->priv, file, il);
		il--;
		tfprintf(file, il, "};\n");
	}

	il--;
	tfprintf(file, il, "};\n");
}

static int filecfg_tempid_dump(FILE * restrict file, unsigned int il, const tempid_t tempid)
{
	if (!file)
		return (-EINVALID);

	if (!hardware_sensor_name(tempid)) {
		fprintf(file, " {};\n");
		return (-EINVALID);
	}

	fprintf(file, " {\n");
	il++;
	tfprintf(file, il, "backend \"%s\";\n", hw_backends_name(tempid.bid));
	tfprintf(file, il, "name \"%s\";\n", hardware_sensor_name(tempid));
	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_relid_dump(FILE * restrict file, unsigned int il, const relid_t relid)
{
	if (!file)
		return (-EINVALID);

	if (!hardware_relay_name(relid)) {
		fprintf(file, " {};\n");
		return (-EINVALID);
	}

	fprintf(file, " {\n");
	il++;
	tfprintf(file, il, "backend \"%s\";\n", hw_backends_name(relid.bid));
	tfprintf(file, il, "name \"%s\";\n", hardware_relay_name(relid));
	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static const char * filecfg_bool_str(const bool test)
{
	return (test ? "yes" : "no");
}

static const char * filecfg_runmode_str(const enum e_runmode runmode)
{
	switch (runmode) {
		case RM_OFF:
			return ("off");
		case RM_AUTO:
			return ("auto");
		case RM_COMFORT:
			return ("comfort");
		case RM_ECO:
			return ("eco");
		case RM_FROSTFREE:
			return ("frostfree");
		case RM_TEST:
			return ("test");
		case RM_DHWONLY:
			return ("dhwonly");
		case RM_UNKNOWN:
		default:
			return ("");
	}
}


static int filecfg_pump_dump(FILE * restrict const file, unsigned int il, const struct s_pump * restrict const pump)
{
	if (!file || !pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "pump \"%s\" {\n", pump->name);
	il++;
	if (FCD_Exhaustive || pump->set.cooldown_time)
		tfprintf(file, il, "cooldown_time %ld;\n", pump->set.cooldown_time);
	tfprintf(file, il, "rid_pump"); filecfg_relid_dump(file, il, pump->set.rid_pump);	// mandatory
	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}


static int filecfg_v_bangbang_dump(FILE * restrict const file, unsigned int il, const struct s_valve * restrict const valve)
{
	return (ALL_OK);
}

static int filecfg_v_sapprox_dump(FILE * restrict const file, unsigned int il, const struct s_valve * restrict const valve)
{
	const struct s_valve_sapprox_priv * restrict priv;

	if (!file || !valve)
		return (-EINVALID);

	if (VA_SAPPROX != valve->set.algo)
		return (-EINVALID);

	priv = valve->priv;

	tfprintf(file, il, "amount %" PRIdFAST16 ";\n", priv->set.amount);
	tfprintf(file, il, "sample_intvl %ld;\n", priv->set.sample_intvl);

	return (ALL_OK);
}

static int filecfg_v_pi_dump(FILE * restrict const file, unsigned int il, const struct s_valve * restrict const valve)
{
	const struct s_valve_pi_priv * restrict priv;

	if (!file || !valve)
		return (-EINVALID);

	if (VA_PI != valve->set.algo)
		return (-EINVALID);

	priv = valve->priv;

	tfprintf(file, il, "sample_intvl %ld;\n", priv->set.sample_intvl);
	tfprintf(file, il, "Tu %ld;\n", priv->set.Tu);
	tfprintf(file, il, "Td %ld;\n", priv->set.Td);
	tfprintf(file, il, "Ksmax %.1f;\n", temp_to_deltaK(priv->set.Ksmax));
	tfprintf(file, il, "tune_f %" PRIdFAST8 ";\n", priv->set.tune_f);

	return (ALL_OK);
}

static int filecfg_valve_algo_dump(FILE * restrict const file, unsigned int il, const struct s_valve * restrict const valve)
{
	const char * algoname;
	int (* privdump)(FILE * restrict const, unsigned int, const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.algo) {
		case VA_BANGBANG:
			algoname = "bangbang";
			privdump = filecfg_v_bangbang_dump;
			break;
		case VA_SAPPROX:
			algoname = "sapprox";
			privdump = filecfg_v_sapprox_dump;
			break;
		case VA_PI:
			algoname = "PI";
			privdump = filecfg_v_pi_dump;
			break;
		case VA_NONE:
		default:
			algoname = "";
			privdump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	fprintf(file, " \"%s\" {\n", algoname);
	il++;
	if (privdump)
		privdump(file, il, valve);
	il--;
	tfprintf(file, il, "};\n");

	return (ret);
}

static int filecfg_valve_dump(FILE * restrict const file, unsigned int il, const struct s_valve * restrict const valve)
{
	if (!file || !valve)
		return (-EINVALID);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "valve \"%s\" {\n", valve->name);
	il++;

	if (FCD_Exhaustive || valve->set.tdeadzone)
		tfprintf(file, il, "tdeadzone %.1f;\n", temp_to_deltaK(valve->set.tdeadzone));
	if (FCD_Exhaustive || valve->set.deadband)
		tfprintf(file, il, "deadband %" PRIdFAST16 ";\n", valve->set.deadband);
	tfprintf(file, il, "ete_time %ld;\n", valve->set.ete_time);				// mandatory
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tid_hot))
		tfprintf(file, il, "tid_hot"), filecfg_tempid_dump(file, il, valve->set.tid_hot);
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tid_cold))
		tfprintf(file, il, "tid_cold"), filecfg_tempid_dump(file, il, valve->set.tid_cold);
	tfprintf(file, il, "tid_out"); filecfg_tempid_dump(file, il, valve->set.tid_out);	// mandatory

	tfprintf(file, il, "rid_hot"); filecfg_relid_dump(file, il, valve->set.rid_hot);	// mandatory
	tfprintf(file, il, "rid_cold"); filecfg_relid_dump(file, il, valve->set.rid_cold);	// mandatory

	tfprintf(file, il, "algo"); filecfg_valve_algo_dump(file, il, valve);			// mandatory

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}


static int filecfg_boiler_hs_dump(FILE * restrict const file, unsigned int il, const struct s_heatsource * restrict const heat)
{
	const char * idlemode;
	const struct s_boiler_priv * restrict priv;
	int ret = ALL_OK;

	if (!file || !heat)
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

	fprintf(file, " {\n");
	il++;

	tfprintf(file, il, "idle_mode \"%s\";\n", idlemode);
	tfprintf(file, il, "hysteresis %.1f;\n", temp_to_deltaK(priv->set.hysteresis));				// mandatory
	tfprintf(file, il, "limit_thardmax %.1f;\n", temp_to_celsius(priv->set.limit_thardmax));		// mandatory
	if (FCD_Exhaustive || priv->set.limit_tmax)
		tfprintf(file, il, "limit_tmax %.1f;\n", temp_to_celsius(priv->set.limit_tmax));
	if (FCD_Exhaustive || priv->set.limit_tmin)
		tfprintf(file, il, "limit_tmin %.1f;\n", temp_to_celsius(priv->set.limit_tmin));
	if (FCD_Exhaustive || priv->set.limit_treturnmin)
		tfprintf(file, il, "limit_treturnmin %.1f;\n", temp_to_celsius(priv->set.limit_treturnmin));
	tfprintf(file, il, "t_freeze %.1f;\n", temp_to_celsius(priv->set.t_freeze));				// mandatory
	if (FCD_Exhaustive || priv->set.burner_min_time)
		tfprintf(file, il, "burner_min_time %ld;\n", priv->set.burner_min_time);

	tfprintf(file, il, "tid_boiler"); filecfg_tempid_dump(file, il, priv->set.tid_boiler);			// mandatory
	if (FCD_Exhaustive || hardware_sensor_name(priv->set.tid_boiler_return))
		tfprintf(file, il, "tid_boiler_return"), filecfg_tempid_dump(file, il, priv->set.tid_boiler_return);
	tfprintf(file, il, "rid_burner_1"); filecfg_relid_dump(file, il, priv->set.rid_burner_1);		// mandatory
	if (FCD_Exhaustive || hardware_relay_name(priv->set.rid_burner_2))
		tfprintf(file, il, "rid_burner_2"), filecfg_relid_dump(file, il, priv->set.rid_burner_2);

	if (FCD_Exhaustive || priv->pump_load)
		tfprintf(file, il, "pump_load \"%s\";\n", priv->pump_load ? priv->pump_load->name : "");
	if (FCD_Exhaustive || priv->valve_ret)
		tfprintf(file, il, "valve_ret \"%s\";\n", priv->valve_ret ? priv->valve_ret->name : "");

	il--;
	tfprintf(file, il, "};\n");

	return (ret);
}

static int filecfg_heatsource_type_dump(FILE * restrict const file, unsigned int il, const struct s_heatsource * restrict const heat)
{
	const char * typename;
	int (*privdump)(FILE * restrict const, unsigned int, const struct s_heatsource * restrict const);
	int ret = ALL_OK;

	switch (heat->set.type) {
		case HS_BOILER:
			typename = "boiler";
			privdump = filecfg_boiler_hs_dump;
			break;
		case HS_NONE:
		default:
			ret = -EINVALID;
			typename = "";
			privdump = NULL;
			break;
	}

	fprintf(file, " \"%s\"", typename);
	if (privdump)
		privdump(file, il, heat);

	return (ret);
}

static int filecfg_heatsource_dump(FILE * restrict const file, unsigned int il, const struct s_heatsource * restrict const heat)
{
	if (!file || !heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "heatsource \"%s\" {\n", heat->name);
	il++;

	tfprintf(file, il, "runmode \"%s\";\n", filecfg_runmode_str(heat->set.runmode));	// mandatory
	tfprintf(file, il, "type"); filecfg_heatsource_type_dump(file, il, heat);		// mandatory
	if (FCD_Exhaustive || heat->set.prio)
		tfprintf(file, il, "prio %hd;\n", heat->set.prio);
	if (FCD_Exhaustive || heat->set.consumer_sdelay)
		tfprintf(file, il, "consumer_sdelay %ld;\n", heat->set.consumer_sdelay);

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_dhwt_params_dump(FILE * restrict const file, unsigned int il, const struct s_dhwt_params * restrict const params)
{
	if (!file || !params)
		return (-EINVALID);

	fprintf(file, " {\n");
	il++;

	if (FCD_Exhaustive || params->limit_chargetime)
		tfprintf(file, il, "limit_chargetime %ld;\n", params->limit_chargetime);
	if (FCD_Exhaustive || params->limit_wintmax)
		tfprintf(file, il, "limit_wintmax %.1f;\n", temp_to_celsius(params->limit_wintmax));
	if (FCD_Exhaustive || params->limit_tmin)
		tfprintf(file, il, "limit_tmin %.1f;\n", temp_to_celsius(params->limit_tmin));
	if (FCD_Exhaustive || params->limit_tmax)
		tfprintf(file, il, "limit_tmax %.1f;\n", temp_to_celsius(params->limit_tmax));

	if (FCD_Exhaustive || params->t_legionella)
		tfprintf(file, il, "t_legionella %.1f;\n", temp_to_celsius(params->t_legionella));
	if (FCD_Exhaustive || params->t_comfort)
		tfprintf(file, il, "t_comfort %.1f;\n", temp_to_celsius(params->t_comfort));
	if (FCD_Exhaustive || params->t_eco)
		tfprintf(file, il, "t_eco %.1f;\n", temp_to_celsius(params->t_eco));
	if (FCD_Exhaustive || params->t_frostfree)
		tfprintf(file, il, "t_frostfree %.1f;\n", temp_to_celsius(params->t_frostfree));

	if (FCD_Exhaustive || params->hysteresis)
		tfprintf(file, il, "hysteresis %.1f;\n", temp_to_deltaK(params->hysteresis));
	if (FCD_Exhaustive || params->temp_inoffset)
		tfprintf(file, il, "temp_inoffset %.1f;\n", temp_to_deltaK(params->temp_inoffset));

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_dhwt_dump(FILE * restrict const file, unsigned int il, const struct s_dhw_tank * restrict const dhwt)
{
	const char * cpriostr, * fmode;
	int ret = ALL_OK;

	if (!file || !dhwt)
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

	tfprintf(file, il, "dhwt \"%s\" {\n", dhwt->name);
	il++;

	if (FCD_Exhaustive || dhwt->set.electric_failover)
		tfprintf(file, il, "electric_failover %s;\n", filecfg_bool_str(dhwt->set.electric_failover));
	if (FCD_Exhaustive || dhwt->set.anti_legionella)
		tfprintf(file, il, "anti_legionella %s;\n", filecfg_bool_str(dhwt->set.anti_legionella));
	if (FCD_Exhaustive || dhwt->set.legionella_recycle)
		tfprintf(file, il, "legionella_recycle %s;\n", filecfg_bool_str(dhwt->set.legionella_recycle));
	if (FCD_Exhaustive || dhwt->set.prio)
		tfprintf(file, il, "prio %hd;\n", dhwt->set.prio);
	tfprintf(file, il, "runmode \"%s\";\n", filecfg_runmode_str(dhwt->set.runmode));		// mandatory
	tfprintf(file, il, "dhwt_cprio \"%s\";\n", cpriostr);
	tfprintf(file, il, "force_mode \"%s\";\n", fmode);

	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_bottom))
		tfprintf(file, il, "tid_bottom"), filecfg_tempid_dump(file, il, dhwt->set.tid_bottom);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_top))
		tfprintf(file, il, "tid_top"), filecfg_tempid_dump(file, il, dhwt->set.tid_top);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_win))
		tfprintf(file, il, "tid_win"), filecfg_tempid_dump(file, il, dhwt->set.tid_win);
	if (FCD_Exhaustive || hardware_sensor_name(dhwt->set.tid_wout))
		tfprintf(file, il, "tid_wout"), filecfg_tempid_dump(file, il, dhwt->set.tid_wout);
	if (FCD_Exhaustive || hardware_relay_name(dhwt->set.rid_selfheater))
		tfprintf(file, il, "rid_selfheater"), filecfg_relid_dump(file, il, dhwt->set.rid_selfheater);

	tfprintf(file, il, "params"); filecfg_dhwt_params_dump(file, il, &dhwt->set.params);

	if (FCD_Exhaustive || dhwt->pump_feed)
		tfprintf(file, il, "pump_feed \"%s\";\n", dhwt->pump_feed ? dhwt->pump_feed->name : "");
	if (FCD_Exhaustive || dhwt->pump_recycle)
		tfprintf(file, il, "pump_recycle \"%s\";\n", dhwt->pump_recycle ? dhwt->pump_recycle->name : "");

	il--;
	tfprintf(file, il, "};\n");

	return (ret);
}


static int filecfg_hc_tlbilin_dump(FILE * restrict const file, unsigned int il, const struct s_hcircuit * restrict const circuit)
{
	const struct s_tlaw_bilin20C_priv * restrict priv;

	if (!file || !circuit)
		return (-EINVALID);

	if (HCL_BILINEAR != circuit->set.tlaw)
		return (-EINVALID);

	priv = circuit->tlaw_priv;

	// all params mandatory
	tfprintf(file, il, "tout1 %.1f;\n", temp_to_celsius(priv->tout1));
	tfprintf(file, il, "twater1 %.1f;\n", temp_to_celsius(priv->twater1));
	tfprintf(file, il, "tout2 %.1f;\n", temp_to_celsius(priv->tout2));
	tfprintf(file, il, "twater2 %.1f;\n", temp_to_celsius(priv->twater2));
	tfprintf(file, il, "nH100 %" PRIdFAST16 ";\n", priv->nH100);

#if 0	// do not print these 'internal' parameters as for now they are not meant to be set externally
	tfprintf(file, il, "toutinfl %.1f;\n", temp_to_celsius(priv->toutinfl));
	tfprintf(file, il, "twaterinfl %.1f;\n", temp_to_celsius(priv->twaterinfl));
	//tfprintf(file, il, "offset %.1f;\n", temp_to_deltaK(priv->offset));	// don't print offset as it's homogenous to internal (meaningless) temperature dimensions
	tfprintf(file, il, "slope %.1f;\n", priv->slope);
#endif
	return (ALL_OK);
}

static int filecfg_hcircuit_tlaw_dump(FILE * restrict const file, unsigned int il, const struct s_hcircuit * restrict const circuit)
{
	const char * tlawname;
	int (*privdump)(FILE * restrict const, unsigned int, const struct s_hcircuit * restrict const);
	int ret = ALL_OK;

	switch (circuit->set.tlaw) {
		case HCL_BILINEAR:
			tlawname = "bilinear";
			privdump = filecfg_hc_tlbilin_dump;
			break;
		case VA_NONE:
		default:
			tlawname = "";
			privdump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	fprintf(file, " \"%s\" {\n", tlawname);
	il++;
	if (privdump)
		privdump(file, il, circuit);
	il--;
	tfprintf(file, il, "};\n");

	return (ret);
}

static int filecfg_hcircuit_params_dump(FILE * restrict const file, unsigned int il, const struct s_hcircuit_params * restrict const params)
{
	if (!file || !params)
		return (-EINVALID);

	fprintf(file, " {\n");
	il++;

	if (FCD_Exhaustive || params->t_comfort)
		tfprintf(file, il, "t_comfort %.1f;\n", temp_to_celsius(params->t_comfort));
	if (FCD_Exhaustive || params->t_eco)
		tfprintf(file, il, "t_eco %.1f;\n", temp_to_celsius(params->t_eco));
	if (FCD_Exhaustive || params->t_frostfree)
		tfprintf(file, il, "t_frostfree %.1f;\n", temp_to_celsius(params->t_frostfree));
	if (FCD_Exhaustive || params->t_offset)
		tfprintf(file, il, "t_offset %.1f;\n", temp_to_deltaK(params->t_offset));

	if (FCD_Exhaustive || params->outhoff_comfort)
		tfprintf(file, il, "outhoff_comfort %.1f;\n", temp_to_celsius(params->outhoff_comfort));
	if (FCD_Exhaustive || params->outhoff_eco)
		tfprintf(file, il, "outhoff_eco %.1f;\n", temp_to_celsius(params->outhoff_eco));
	if (FCD_Exhaustive || params->outhoff_frostfree)
		tfprintf(file, il, "outhoff_frostfree %.1f;\n", temp_to_celsius(params->outhoff_frostfree));
	if (FCD_Exhaustive || params->outhoff_hysteresis)
		tfprintf(file, il, "outhoff_hysteresis %.1f;\n", temp_to_deltaK(params->outhoff_hysteresis));

	if (FCD_Exhaustive || params->limit_wtmin)
		tfprintf(file, il, "limit_wtmin %.1f;\n", temp_to_celsius(params->limit_wtmin));
	if (FCD_Exhaustive || params->limit_wtmax)
		tfprintf(file, il, "limit_wtmax %.1f;\n", temp_to_celsius(params->limit_wtmax));

	if (FCD_Exhaustive || params->temp_inoffset)
		tfprintf(file, il, "temp_inoffset %.1f;\n", temp_to_deltaK(params->temp_inoffset));

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_hcircuit_dump(FILE * restrict const file, unsigned int il, const struct s_hcircuit * restrict const circuit)
{
	if (!file || !circuit)
		return (-EINVALID);

	if (!circuit->set.configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "hcircuit \"%s\" {\n", circuit->name);
	il++;

	if (FCD_Exhaustive || circuit->set.fast_cooldown)
		tfprintf(file, il, "fast_cooldown %s;\n", filecfg_bool_str(circuit->set.fast_cooldown));
	if (FCD_Exhaustive || circuit->set.logging)
		tfprintf(file, il, "logging %s;\n", filecfg_bool_str(circuit->set.logging));
	tfprintf(file, il, "runmode \"%s\";\n", filecfg_runmode_str(circuit->set.runmode));			// mandatory
	if (FCD_Exhaustive || circuit->set.ambient_factor)
		tfprintf(file, il, "ambient_factor %" PRIdFAST16 ";\n", circuit->set.ambient_factor);
	if (FCD_Exhaustive || circuit->set.wtemp_rorh)
		tfprintf(file, il, "wtemp_rorh %.1f;\n", temp_to_deltaK(circuit->set.wtemp_rorh));
	if (FCD_Exhaustive || circuit->set.am_tambient_tK)
		tfprintf(file, il, "am_tambient_tK %ld;\n", circuit->set.am_tambient_tK);
	if (FCD_Exhaustive || circuit->set.tambient_boostdelta)
		tfprintf(file, il, "tambient_boostdelta %.1f;\n", temp_to_deltaK(circuit->set.tambient_boostdelta));
	if (FCD_Exhaustive || circuit->set.boost_maxtime)
		tfprintf(file, il, "boost_maxtime %ld;\n", circuit->set.boost_maxtime);

	tfprintf(file, il, "tid_outgoing"); filecfg_tempid_dump(file, il, circuit->set.tid_outgoing);		// mandatory
	if (FCD_Exhaustive || hardware_sensor_name(circuit->set.tid_return))
		tfprintf(file, il, "tid_return"), filecfg_tempid_dump(file, il, circuit->set.tid_return);
	if (FCD_Exhaustive || hardware_sensor_name(circuit->set.tid_ambient))
		tfprintf(file, il, "tid_ambient"), filecfg_tempid_dump(file, il, circuit->set.tid_ambient);

	tfprintf(file, il, "params"); filecfg_hcircuit_params_dump(file, il, &circuit->set.params);

	tfprintf(file, il, "tlaw"); filecfg_hcircuit_tlaw_dump(file, il, circuit);				// mandatory

	if (FCD_Exhaustive || circuit->valve_mix)
		tfprintf(file, il, "valve_mix \"%s\";\n", circuit->valve_mix ? circuit->valve_mix->name : "");
	if (FCD_Exhaustive || circuit->pump_feed)
		tfprintf(file, il, "pump_feed \"%s\";\n", circuit->pump_feed ? circuit->pump_feed->name : "");
	if (FCD_Exhaustive || circuit->bmodel)
		tfprintf(file, il, "bmodel \"%s\";\n", circuit->bmodel ? circuit->bmodel->name : "");

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_bmodel_dump(FILE * restrict const file, unsigned int il, const struct s_bmodel * restrict const bmodel)
{
	if (!file || !bmodel)
		return (-EINVALID);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "bmodel \"%s\" {\n", bmodel->name);
	il++;

	if (FCD_Exhaustive || bmodel->set.logging)
		tfprintf(file, il, "logging %s;\n", filecfg_bool_str(bmodel->set.logging));
	tfprintf(file, il, "tau %ld;\n", bmodel->set.tau);						// mandatory
	tfprintf(file, il, "tid_outdoor"); filecfg_tempid_dump(file, il, bmodel->set.tid_outdoor);	// mandatory

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_models_dump(FILE* restrict const file, unsigned int il, const struct s_models * restrict const models)
{
	struct s_bmodel_l * restrict bmodelelmt;

	tfprintf(file, il, "models {\n");
	il++;
	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		filecfg_bmodel_dump(file, il, bmodelelmt->bmodel);
	}
	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_config_dump(FILE * restrict const file, unsigned int il, const struct s_config * restrict const config)
{
	if (!file || !config)
		return (-EINVALID);

	tfprintf(file, il, "defconfig {\n");
	il++;

	if (FCD_Exhaustive || config->summer_maintenance)
		tfprintf(file, il, "summer_maintenance %s;\n", filecfg_bool_str(config->summer_maintenance));
	if (FCD_Exhaustive || config->logging)
		tfprintf(file, il, "logging %s;\n", filecfg_bool_str(config->logging));
	if (FCD_Exhaustive || config->limit_tsummer)
		tfprintf(file, il, "limit_tsummer %.1f;\n", temp_to_celsius(config->limit_tsummer));
	if (FCD_Exhaustive || config->limit_tfrost)
		tfprintf(file, il, "limit_tfrost %.1f;\n", temp_to_celsius(config->limit_tfrost));
	if (FCD_Exhaustive || config->sleeping_delay)
		tfprintf(file, il, "sleeping_delay %ld;\n", config->sleeping_delay);

	tfprintf(file, il, "def_hcircuit"); filecfg_hcircuit_params_dump(file, il, &config->def_hcircuit);
	tfprintf(file, il, "def_dhwt"); filecfg_dhwt_params_dump(file, il, &config->def_dhwt);

	il--;
	tfprintf(file, il, "};\n");

	return (ALL_OK);
}

static int filecfg_plant_dump(FILE * restrict const file, unsigned int il, const struct s_plant * restrict const plant)
{
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heatsource_l * heatsl;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;

	if (!file || !plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	tfprintf(file, il, "plant {\n");
	il++;

	if (FCD_Exhaustive || plant->pump_head) {
		tfprintf(file, il, "pumps {\n");
		il++;
		for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next)
			filecfg_pump_dump(file, il, pumpl->pump);
		il--;
		tfprintf(file, il, "};\n");	// pumps
	}

	if (FCD_Exhaustive || plant->valve_head) {
		tfprintf(file, il, "valves {\n");
		il++;
		for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next)
			filecfg_valve_dump(file, il, valvel->valve);
		il--;
		tfprintf(file, il, "};\n");	// valves
	}

	if (FCD_Exhaustive || plant->heats_head) {
		tfprintf(file, il, "heatsources {\n");
		il++;
		for (heatsl = plant->heats_head; heatsl != NULL; heatsl = heatsl->next)
			filecfg_heatsource_dump(file, il, heatsl->heats);
		il--;
		tfprintf(file, il, "};\n");	// heatsources
	}

	if (FCD_Exhaustive || plant->circuit_head) {
		tfprintf(file, il, "hcircuits {\n");
		il++;
		for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next)
			filecfg_hcircuit_dump(file, il, circuitl->circuit);
		il--;
		tfprintf(file, il, "};\n");	// heating_circuits
	}

	if (FCD_Exhaustive || plant->dhwt_head) {
		tfprintf(file, il, "dhwts {\n");
		il++;
		for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next)
			filecfg_dhwt_dump(file, il, dhwtl->dhwt);
		il--;
		tfprintf(file, il, "};\n");	// dhwts
	}

	il--;
	tfprintf(file, il, "};\n");	// plant

	return (ALL_OK);
}

/**
 * Dump system configuration to file.
 * This function will dump the complete system configuration to the file
 * specified in #FILECONFIG_NAME under the path #RWCHCD_STORAGE_PATH.
 * @return exec status
 */
int filecfg_dump(void)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	FILE * restrict file = NULL;
	unsigned int il = 0;

	// make sure we're in target wd
	if (chdir(RWCHCD_STORAGE_PATH))
		return (-ESTORE);

	// open stream
	file = fopen(FILECONFIG_NAME, "w");
	if (!file)
		return (-ESTORE);

	// dump backends
	filecfg_backends_dump(file, il);

	// dump runtime config
	filecfg_config_dump(file, il, runtime->config);

	// dump models
	filecfg_models_dump(file, il, models_get());

	// dump plant
	filecfg_plant_dump(file, il, runtime->plant);

	fclose(file);

	return (ALL_OK);
}
