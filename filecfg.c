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

#include "lib.h"
#include "hw_backends.h"
#include "hardware.h"
#include "filecfg/pump_dump.h"
#include "filecfg/valve_dump.h"
#include "filecfg/heatsource_dump.h"
#include "hcircuit.h"
#include "filecfg/dhwt_dump.h"
#include "models.h"
#include "config.h"
#include "runtime.h"
#include "plant.h"
#include "scheduler.h"
#include "filecfg.h"
#include "timekeep.h"

#include "filecfg/scheduler_dump.h"
#include "filecfg/models_dump.h"
#include "filecfg/storage_dump.h"
#include "log_filecfg.h"

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

int filecfg_tempid_dump(const tempid_t tempid)
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

int filecfg_relid_dump(const relid_t relid)
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

/**
 * Print filecfg representation of a given runmode.
 * @param runmode the value to represent
 * @return a statically allocated string
 */
const char * filecfg_sysmode_str(const enum e_systemmode sysmode)
{
	switch (sysmode) {
		case SYS_OFF:
			return ("off");
		case SYS_AUTO:
			return ("auto");
		case SYS_COMFORT:
			return ("comfort");
		case SYS_ECO:
			return ("eco");
		case SYS_FROSTFREE:
			return ("frostfree");
		case SYS_TEST:
			return ("test");
		case SYS_DHWONLY:
			return ("dhwonly");
		case SYS_MANUAL:
			return ("manual");
		case SYS_NONE:
		case SYS_UNKNOWN:
		default:
			return ("");
	}
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
	if (FCD_Exhaustive || config->summer_run_interval)
		filecfg_iprintf("summer_run_interval %ld;\n", timekeep_tk_to_sec(config->summer_run_interval));
	if (FCD_Exhaustive || config->summer_run_duration)
		filecfg_iprintf("summer_run_duration %ld;\n", timekeep_tk_to_sec(config->summer_run_duration));
	filecfg_iprintf("startup_sysmode \"%s\";\n", filecfg_sysmode_str(config->startup_sysmode));	// mandatory
	filecfg_iprintf("startup_runmode \"%s\";\n", filecfg_runmode_str(config->startup_runmode));	// mandatory if SYS_MANUAL
	filecfg_iprintf("startup_dhwmode \"%s\";\n", filecfg_runmode_str(config->startup_runmode));	// mandatory if SYS_MANUAL

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
 * specified in #FILECONFIG_NAME under the storage path.
 * @return exec status
 */
int filecfg_dump(void)
{
	const struct s_runtime * restrict const runtime = runtime_get();

	// XXX the storage subsystem ensures we're in target wd

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
	filecfg_models_dump();

	// dump plant
	filecfg_plant_dump(runtime->plant);

	// dump storage
	filecfg_storage_dump();

	// dump logging
	log_filecfg_dump();

	// dump scheduler
	filecfg_scheduler_dump();

	fclose(FCD_File);
	FCD_File = NULL;

	return (ALL_OK);
}
