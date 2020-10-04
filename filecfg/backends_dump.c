//
//  filecfg/backends_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration dumping.
 */

#include "backends_dump.h"
#include "filecfg_dump.h"
#include "hw_backends.h"

extern struct s_hw_backends HW_backends;

/**
 * Return a backend name.
 * @param bid target backend id
 * @return target backend name or NULL if error.
 */
static const char * hw_backends_name(const bid_t bid)
{
	if (bid >= HW_backends.last)
		return (NULL);

	return (HW_backends.all[bid].name);
}

void filecfg_backends_dump()
{
	unsigned int id;

	filecfg_iprintf("backends {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < HW_backends.last; id++) {
		filecfg_iprintf("backend \"%s\" {\n", HW_backends.all[id].name);
		filecfg_ilevel_inc();
		if (HW_backends.all[id].cb->filecfg_dump)
			HW_backends.all[id].cb->filecfg_dump(HW_backends.all[id].priv);
		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Return hardware sensor name.
 * @param tempid id of the target hardware sensor
 * @return target hardware sensor name or NULL if error
 */
static const char * hardware_sensor_name(const tempid_t tempid)
{
	const bid_t bid = tempid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->input_name(HW_backends.all[bid].priv, HW_INPUT_TEMP, tempid.sid));
}

/**
 * Return hardware relay name.
 * @param relid id of the target hardware relay
 * @return target hardware relay name or NULL if error
 */
static const char * hardware_relay_name(const relid_t relid)
{
	const bid_t bid = relid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->output_name(HW_backends.all[bid].priv, HW_OUTPUT_RELAY, relid.rid));
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
