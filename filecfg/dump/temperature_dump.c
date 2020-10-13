//
//  filecfg/dump/temperature_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Temperature file configuration dumping.
 */

#include "filecfg_dump.h"
#include "backends_dump.h"
#include "io/inputs/temperature.h"

static const char * const temp_op_str[] = {
	[T_OP_FIRST]	= "first",
	[T_OP_MIN]	= "min",
	[T_OP_MAX]	= "max",
};

static const char * const temp_miss_str[] = {
	[T_MISS_FAIL]	= "fail",
	[T_MISS_IGN]	= "ignore",
	[T_MISS_IGNDEF]	= "ignoredef",
};

void filecfg_temperature_dump(const struct s_temperature * t)
{
	unsigned int i;

	if (!t->set.configured)
		return;

	filecfg_iprintf("temperature \"%s\" {\n", t->name);
	filecfg_ilevel_inc();

	filecfg_dump_tk("period", t->set.period);
	if (FCD_Exhaustive || t->set.igntemp)
		filecfg_dump_celsius("igntemp", t->set.igntemp);
	filecfg_dump_nodestr("op", temp_op_str[t->set.op]);
	filecfg_dump_nodestr("missing", temp_miss_str[t->set.missing]);

	filecfg_iprintf("sources {\n");
	filecfg_ilevel_inc();

	for (i = 0; i < t->tlast; i++)
		filecfg_backends_dump_temperature("source", t->tlist[i]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}
