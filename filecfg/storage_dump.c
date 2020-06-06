//
//  filecfg/storage_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Storage subsystem file configuration dumping.
 */

#include "storage_dump.h"
#include "filecfg_dump.h"

extern bool Storage_configured;
extern const char * Storage_path;

/**
 * Dump the storage configuration to file.
 * @return exec status
 * @warning not thread safe
 */
int filecfg_storage_dump(void)
{
	if (!Storage_path || !Storage_configured)
		return (-EINVALID);

	filecfg_iprintf("storage {\n");
	filecfg_ilevel_inc();
	filecfg_dump_nodestr("path", Storage_path);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
