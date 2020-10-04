//
//  filecfg/dump/backends_dump.c
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
 * Return hardware input name.
 * @param binid id of the target hardware input
 * @return target hardware input name or NULL if error
 */
static const char * hardware_input_name(const enum e_hw_input_type type, const binid_t binid)
{
	const bid_t bid = binid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->input_name(HW_backends.all[bid].priv, type, binid.inid));
}

/**
 * Return hardware output name.
 * @param boutid id of the target hardware output
 * @return target hardware output name or NULL if error
 */
static const char * hardware_output_name(const enum e_hw_output_type type, const boutid_t boutid)
{
	const bid_t bid = boutid.bid;

	// make sure bid is valid
	if (unlikely(HW_backends.last <= bid))
		return (NULL);

	// call backend callback - input sanitizing left to cb
	return (HW_backends.all[bid].cb->output_name(HW_backends.all[bid].priv, type, boutid.outid));
}

int filecfg_backends_dump_binid(const enum e_hw_input_type type, const char *name, const binid_t tempid)
{
	if (!hardware_input_name(type, tempid)) {
		filecfg_printf("%s {};\n", name);
		return (-EINVALID);
	}

	filecfg_iprintf("%s {\n", name);
	filecfg_ilevel_inc();
	filecfg_dump_nodestr("backend", hw_backends_name(tempid.bid));
	filecfg_dump_nodestr("name", hardware_input_name(type, tempid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int filecfg_backends_dump_boutid(const enum e_hw_output_type type, const char *name, const boutid_t boutid)
{
	if (!hardware_output_name(type, boutid)) {
		filecfg_printf("%s {};\n", name);
		return (-EINVALID);
	}

	filecfg_iprintf("%s {\n", name);
	filecfg_ilevel_inc();
	filecfg_dump_nodestr("backend", hw_backends_name(boutid.bid));
	filecfg_dump_nodestr("name", hardware_output_name(type, boutid));
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
