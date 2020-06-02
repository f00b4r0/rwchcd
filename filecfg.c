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
#include "filecfg/dhwt_dump.h"
#include "filecfg/hcircuit_dump.h"
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
#include "filecfg/log_dump.h"

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
	filecfg_log_dump();

	// dump scheduler
	filecfg_scheduler_dump();

	fclose(FCD_File);
	FCD_File = NULL;

	return (ALL_OK);
}
