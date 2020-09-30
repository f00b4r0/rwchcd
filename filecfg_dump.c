//
//  filecfg_dump.c
//  rwchcd
//
//  (C) 2018-2020 Thibaut VARENE
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
#include "runtime.h"

#include "filecfg_dump.h"
#include "filecfg/scheduler_dump.h"
#include "filecfg/models_dump.h"
#include "filecfg/storage_dump.h"
#include "filecfg/log_dump.h"
#include "filecfg/plant_dump.h"
#include "filecfg/backends_dump.h"
#include "filecfg/inputs_dump.h"
#include "filecfg/outputs_dump.h"

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

int filecfg_dump_nodestr(const char *name, const char *value)
{
	return (filecfg_iprintf("%s \"%s\";\n", name, value));
}

int filecfg_dump_tempid(const char *name, const tempid_t tempid)
{
	if (!hardware_sensor_name(tempid)) {
		filecfg_printf("%s {};\n", name);
		return (-EINVALID);
	}

	filecfg_iprintf("%s {\n", name);
	filecfg_ilevel_inc();
	filecfg_dump_nodestr("backend", hw_backends_name(tempid.bid));
	filecfg_dump_nodestr("name", hardware_sensor_name(tempid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_dump_relid(const char *name, const relid_t relid)
{
	if (!hardware_relay_name(relid)) {
		filecfg_printf("%s {};\n", name);
		return (-EINVALID);
	}

	filecfg_iprintf("%s {\n", name);
	filecfg_ilevel_inc();
	filecfg_dump_nodestr("backend", hw_backends_name(relid.bid));
	filecfg_dump_nodestr("name", hardware_relay_name(relid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

/**
 * Print filecfg representation of a bool.
 * @param test the value to represent
 * @return a statically allocated string
 */
static const char * filecfg_bool_str(const bool test)
{
	return (test ? "yes" : "no");
}

int filecfg_dump_nodebool(const char *name, bool value)
{
	return (filecfg_iprintf("%s %s;\n", name, filecfg_bool_str(value)));
}

int filecfg_dump_celsius(const char *name, temp_t value)
{
	return (filecfg_iprintf("%s %.1f;\n", name, temp_to_celsius(value)));
}

int filecfg_dump_deltaK(const char *name, temp_t value)
{
	return (filecfg_iprintf("%s %.1f;\n", name, temp_to_deltaK(value)));
}

int filecfg_dump_tk(const char *name, timekeep_t value)
{
	return (filecfg_iprintf("%s %ld;\n", name, timekeep_tk_to_sec(value)));
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

static int runtime_config_dump(const struct s_runtime * restrict const runtime)
{
	if (!runtime)
		return (-EINVALID);

	filecfg_iprintf("defconfig {\n");
	filecfg_ilevel_inc();

	filecfg_dump_nodestr("startup_sysmode", filecfg_sysmode_str(runtime->set.startup_sysmode));	// mandatory
	filecfg_dump_nodestr("startup_runmode", filecfg_runmode_str(runtime->set.startup_runmode));	// mandatory if SYS_MANUAL
	filecfg_dump_nodestr("startup_dhwmode", filecfg_runmode_str(runtime->set.startup_runmode));	// mandatory if SYS_MANUAL

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

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

	// dump inputs
	filecfg_inputs_dump();

	// dump outputs
	filecfg_outputs_dump();

	// dump runtime config
	runtime_config_dump(runtime);

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
