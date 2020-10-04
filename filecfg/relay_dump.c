//
//  filecfg/relay_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Relay file configuration dumping.
 */

#include "filecfg_dump.h"
#include "backends_dump.h"
#include "relay.h"

static const char * const relay_op_str[] = {
	[R_OP_FIRST]	= "first",
	[R_OP_ALL]	= "all",
};

static const char * const relay_miss_str[] = {
	[R_MISS_FAIL]	= "fail",
	[R_MISS_IGN]	= "ignore",
};

void filecfg_relay_dump(const struct s_relay * r)
{
	unsigned int i;

	if (!r->set.configured)
		return;

	filecfg_iprintf("relay \"%s\" {\n", r->name);
	filecfg_ilevel_inc();

	filecfg_dump_nodestr("op", relay_op_str[r->set.op]);
	filecfg_dump_nodestr("missing", relay_miss_str[r->set.missing]);

	filecfg_iprintf("targets {\n");
	filecfg_ilevel_inc();

	for (i = 0; i < r->rlast; i++)
		filecfg_dump_relid("target", r->rlist[i]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}
