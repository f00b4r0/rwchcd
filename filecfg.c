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
#include <stdarg.h>

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
#include "scheduler.h"
#include "filecfg.h"
#include "timekeep.h"

#define FILECONFIG_NAME		"dumpcfg.txt"	///< target file for configuration dump

bool FCD_Exhaustive = false;

static FILE * FCD_File = NULL;		///< pointer to target configuration file (for dump).
static unsigned int FCD_ilevel;		///< current indentation level

/**
 * Programmatically indent with tabs.
 * @param level desired indentation level
 * @return a string containing the required number of '\\t'
 */
static const char * filecfg_tabs(const unsigned int level)
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

/**
 * fprintf() wrapper to config file output.
 * This function will write to the set #FCD_File and handle
 * indentation level based on the value of #FCD_ilevel.
 * @param indent true if the output should be indented
 * @param format the printf-style format style
 * @return exec status
 */
int filecfg_printf_wrapper(const bool indent, const char * restrict format, ...)
{
	FILE * file = FCD_File;
	int ret;
	va_list args;

	if (!file)
		return (-EINVALID);

	va_start(args, format);

	if (indent)
		fprintf(file, "%s", filecfg_tabs(FCD_ilevel));
	ret = vfprintf(file, format, args);

	va_end(args);

	return (ret);
}

/** Increase indentation level */
int filecfg_ilevel_inc(void)
{
	FCD_ilevel++;
	return (ALL_OK);
}

/** Decrease indentation level */
int filecfg_ilevel_dec(void)
{
	if (!FCD_ilevel)
		return (-EINVALID);

	FCD_ilevel--;

	return (ALL_OK);
}

static void filecfg_backends_dump()
{
	unsigned int id;

	filecfg_iprintf("backends {\n");
	filecfg_ilevel_inc();

	for (id = 0; (id < ARRAY_SIZE(HW_backends) && HW_backends[id]); id++) {
		filecfg_iprintf("backend \"%s\" {\n", HW_backends[id]->name);
		filecfg_ilevel_inc();
		if (HW_backends[id]->cb->filecfg_dump)
			HW_backends[id]->cb->filecfg_dump(HW_backends[id]->priv);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

static int filecfg_tempid_dump(const tempid_t tempid)
{
	if (!hardware_sensor_name(tempid)) {
		filecfg_printf(" {};\n");
		return (-EINVALID);
	}

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();
	filecfg_iprintf("backend \"%s\";\n", hw_backends_name(tempid.bid));
	filecfg_iprintf("name \"%s\";\n", hardware_sensor_name(tempid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

static int filecfg_relid_dump(const relid_t relid)
{
	if (!hardware_relay_name(relid)) {
		filecfg_printf(" {};\n");
		return (-EINVALID);
	}

	filecfg_printf(" {\n");
	filecfg_ilevel_inc();
	filecfg_iprintf("backend \"%s\";\n", hw_backends_name(relid.bid));
	filecfg_iprintf("name \"%s\";\n", hardware_relay_name(relid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

/**
 * Print filecfg representation of a bool.
 * @param test the value to represent
 * @return a statically allocated string
 */
const char * filecfg_bool_str(const bool test)
{
	return (test ? "yes" : "no");
}

/**
 * Print filecfg representation of a given runmode.
 * @param runmode the value to represent
 * @return a statically allocated string
 */
const char * filecfg_runmode_str(const enum e_runmode runmode)
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


static int filecfg_pump_dump(const struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("pump \"%s\" {\n", pump->name);
	filecfg_ilevel_inc();
	if (FCD_Exhaustive || pump->set.cooldown_time)
		filecfg_iprintf("cooldown_time %ld;\n", timekeep_tk_to_sec(pump->set.cooldown_time));
	filecfg_iprintf("rid_pump"); filecfg_relid_dump(pump->set.rid_pump);	// mandatory
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}


static int filecfg_v_bangbang_dump(const struct s_valve * restrict const valve)
{
	return (ALL_OK);
}

static int filecfg_v_sapprox_dump(const struct s_valve * restrict const valve)
{
	const struct s_valve_sapprox_priv * restrict priv;

	if (!valve)
		return (-EINVALID);

	if (VA_SAPPROX != valve->set.tset.tmix.algo)
		return (-EINVALID);

	priv = valve->priv;

	filecfg_iprintf("amount %" PRIdFAST16 ";\n", priv->set.amount);
	filecfg_iprintf("sample_intvl %ld;\n", timekeep_tk_to_sec(priv->set.sample_intvl));

	return (ALL_OK);
}

static int filecfg_v_pi_dump(const struct s_valve * restrict const valve)
{
	const struct s_valve_pi_priv * restrict priv;

	if (!valve)
		return (-EINVALID);

	if (VA_PI != valve->set.tset.tmix.algo)
		return (-EINVALID);

	priv = valve->priv;

	filecfg_iprintf("sample_intvl %ld;\n", timekeep_tk_to_sec(priv->set.sample_intvl));
	filecfg_iprintf("Tu %ld;\n", timekeep_tk_to_sec(priv->set.Tu));
	filecfg_iprintf("Td %ld;\n", timekeep_tk_to_sec(priv->set.Td));
	filecfg_iprintf("Ksmax %.1f;\n", temp_to_deltaK(priv->set.Ksmax));
	filecfg_iprintf("tune_f %" PRIdFAST8 ";\n", priv->set.tune_f);

	return (ALL_OK);
}

static int filecfg_valve_algo_dump(const struct s_valve * restrict const valve)
{
	const char * algoname;
	int (* privdump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.tset.tmix.algo) {
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

	filecfg_printf(" \"%s\" {\n", algoname);
	filecfg_ilevel_inc();
	if (privdump)
		ret = privdump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_valve_tmix_dump(const struct s_valve * restrict const valve)
{
	if (FCD_Exhaustive || valve->set.tset.tmix.tdeadzone)
		filecfg_iprintf("tdeadzone %.1f;\n", temp_to_deltaK(valve->set.tset.tmix.tdeadzone));
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tset.tmix.tid_hot))
		filecfg_iprintf("tid_hot"), filecfg_tempid_dump(valve->set.tset.tmix.tid_hot);
	if (FCD_Exhaustive || hardware_sensor_name(valve->set.tset.tmix.tid_cold))
		filecfg_iprintf("tid_cold"), filecfg_tempid_dump(valve->set.tset.tmix.tid_cold);
	filecfg_iprintf("tid_out"); filecfg_tempid_dump(valve->set.tset.tmix.tid_out);		// mandatory

	filecfg_iprintf("algo");
	return (filecfg_valve_algo_dump(valve));			// mandatory
}

static int filecfg_valve_type_dump(const struct s_valve * restrict const valve)
{
	const char * tname;
	int (* vtypedump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.type) {
		case VA_TYPE_MIX:
			tname = "mix";
			vtypedump = filecfg_valve_tmix_dump;
			break;
		case VA_TYPE_NONE:
		default:
			tname = "";
			vtypedump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", tname);
	filecfg_ilevel_inc();
	if (vtypedump)
		ret = vtypedump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_valve_m3way_dump(const struct s_valve * restrict const valve)
{
	filecfg_iprintf("rid_open"); filecfg_relid_dump(valve->set.mset.m3way.rid_open);	// mandatory
	filecfg_iprintf("rid_close"); filecfg_relid_dump(valve->set.mset.m3way.rid_close);	// mandatory

	return (ALL_OK);
}

static int filecfg_valve_m2way_dump(const struct s_valve * restrict const valve)
{
	filecfg_iprintf("rid_trigger"); filecfg_relid_dump(valve->set.mset.m2way.rid_trigger);	// mandatory
	filecfg_iprintf("trigger_opens %s;\n", filecfg_bool_str(valve->set.mset.m2way.trigger_opens));// mandatory

	return (ALL_OK);
}

static int filecfg_valve_motor_dump(const struct s_valve * restrict const valve)
{
	const char * mname;
	int (* vmotordump)(const struct s_valve * restrict const);
	int ret = ALL_OK;

	switch (valve->set.motor) {
		case VA_M_3WAY:
			mname = "3way";
			vmotordump = filecfg_valve_m3way_dump;
			break;
		case VA_M_2WAY:
			mname = "2way";
			vmotordump = filecfg_valve_m2way_dump;
			break;
		case VA_M_NONE:
		default:
			mname = "";
			vmotordump = NULL;
			ret = -EMISCONFIGURED;
			break;
	}

	filecfg_printf(" \"%s\" {\n", mname);
	filecfg_ilevel_inc();
	if (vmotordump)
		ret = vmotordump(valve);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_valve_dump(const struct s_valve * restrict const valve)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("valve \"%s\" {\n", valve->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || valve->set.deadband)
		filecfg_iprintf("deadband %" PRIdFAST16 ";\n", valve->set.deadband);
	filecfg_iprintf("ete_time %ld;\n", timekeep_tk_to_sec(valve->set.ete_time));	// mandatory

	filecfg_iprintf("type"); filecfg_valve_type_dump(valve);			// mandatory
	filecfg_iprintf("motor"); filecfg_valve_motor_dump(valve);			// mandatory

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}


static int filecfg_boiler_hs_dump(const struct s_heatsource * restrict const heat)
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

	if (FCD_Exhaustive || priv->pump_load)
		filecfg_iprintf("pump_load \"%s\";\n", priv->pump_load ? priv->pump_load->name : "");
	if (FCD_Exhaustive || priv->valve_ret)
		filecfg_iprintf("valve_ret \"%s\";\n", priv->valve_ret ? priv->valve_ret->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}

static int filecfg_heatsource_type_dump(const struct s_heatsource * restrict const heat)
{
	const char * typename;
	int (*privdump)(const struct s_heatsource * restrict const);
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

	filecfg_printf(" \"%s\"", typename);
	if (privdump)
		privdump(heat);

	return (ret);
}

static int filecfg_heatsource_dump(const struct s_heatsource * restrict const heat)
{
	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("heatsource \"%s\" {\n", heat->name);
	filecfg_ilevel_inc();

	filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(heat->set.runmode));	// mandatory
	filecfg_iprintf("type"); filecfg_heatsource_type_dump(heat);			// mandatory
	if (FCD_Exhaustive || heat->set.prio)
		filecfg_iprintf("prio %hd;\n", heat->set.prio);
	if (FCD_Exhaustive || heat->set.consumer_sdelay)
		filecfg_iprintf("consumer_sdelay %ld;\n", timekeep_tk_to_sec(heat->set.consumer_sdelay));

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

static int filecfg_dhwt_params_dump(const struct s_dhwt_params * restrict const params)
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

static int filecfg_dhwt_dump(const struct s_dhw_tank * restrict const dhwt)
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
	if (FCD_Exhaustive || dhwt->set.prio)
		filecfg_iprintf("prio %hd;\n", dhwt->set.prio);
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

	if (FCD_Exhaustive || dhwt->pump_feed)
		filecfg_iprintf("pump_feed \"%s\";\n", dhwt->pump_feed ? dhwt->pump_feed->name : "");
	if (FCD_Exhaustive || dhwt->pump_recycle)
		filecfg_iprintf("pump_recycle \"%s\";\n", dhwt->pump_recycle ? dhwt->pump_recycle->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ret);
}


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
		case VA_NONE:
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

static int filecfg_hcircuit_params_dump(const struct s_hcircuit_params * restrict const params)
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

static int filecfg_hcircuit_dump(const struct s_hcircuit * restrict const circuit)
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

	if (FCD_Exhaustive || circuit->valve_mix)
		filecfg_iprintf("valve_mix \"%s\";\n", circuit->valve_mix ? circuit->valve_mix->name : "");
	if (FCD_Exhaustive || circuit->pump_feed)
		filecfg_iprintf("pump_feed \"%s\";\n", circuit->pump_feed ? circuit->pump_feed->name : "");
	if (FCD_Exhaustive || circuit->bmodel)
		filecfg_iprintf("bmodel \"%s\";\n", circuit->bmodel ? circuit->bmodel->name : "");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

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

static int filecfg_models_dump(const struct s_models * restrict const models)
{
	struct s_bmodel_l * restrict bmodelelmt;

	filecfg_iprintf("models {\n");
	filecfg_ilevel_inc();
	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		filecfg_bmodel_dump(bmodelelmt->bmodel);
	}
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

static int filecfg_config_dump(const struct s_config * restrict const config)
{
	if (!config)
		return (-EINVALID);

	filecfg_iprintf("defconfig {\n");
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || config->summer_maintenance)
		filecfg_iprintf("summer_maintenance %s;\n", filecfg_bool_str(config->summer_maintenance));
	if (FCD_Exhaustive || config->logging)
		filecfg_iprintf("logging %s;\n", filecfg_bool_str(config->logging));
	if (FCD_Exhaustive || config->limit_tsummer)
		filecfg_iprintf("limit_tsummer %.1f;\n", temp_to_celsius(config->limit_tsummer));
	if (FCD_Exhaustive || config->limit_tfrost)
		filecfg_iprintf("limit_tfrost %.1f;\n", temp_to_celsius(config->limit_tfrost));
	if (FCD_Exhaustive || config->sleeping_delay)
		filecfg_iprintf("sleeping_delay %ld;\n", timekeep_tk_to_sec(config->sleeping_delay));

	filecfg_iprintf("def_hcircuit"); filecfg_hcircuit_params_dump(&config->def_hcircuit);
	filecfg_iprintf("def_dhwt"); filecfg_dhwt_params_dump(&config->def_dhwt);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

static int filecfg_plant_dump(const struct s_plant * restrict const plant)
{
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heatsource_l * heatsl;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("plant {\n");
	filecfg_ilevel_inc();

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

/**
 * Dump system configuration to file.
 * This function will dump the complete system configuration to the file
 * specified in #FILECONFIG_NAME under the path #RWCHCD_STORAGE_PATH.
 * @return exec status
 */
int filecfg_dump(void)
{
	const struct s_runtime * restrict const runtime = runtime_get();

	// make sure we're in target wd
	if (chdir(RWCHCD_STORAGE_PATH))
		return (-ESTORE);

	// open stream
	FCD_File = fopen(FILECONFIG_NAME, "w");
	if (!FCD_File)
		return (-ESTORE);

	FCD_ilevel = 0;

	// dump backends
	filecfg_backends_dump();

	// dump runtime config
	filecfg_config_dump(runtime->config);

	// dump models
	filecfg_models_dump(models_get());

	// dump plant
	filecfg_plant_dump(runtime->plant);

	// dump scheduler
	scheduler_filecfg_dump();

	fclose(FCD_File);
	FCD_File = NULL;

	return (ALL_OK);
}
