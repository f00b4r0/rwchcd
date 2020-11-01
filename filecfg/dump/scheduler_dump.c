//
//  filecfg/dump/scheduler_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Scheduler subsystem file configuration dumping.
 */

#include "scheduler_dump.h"
#include "scheduler.h"
#include "filecfg_dump.h"

extern struct s_schedules Schedules;

static void scheduler_entry_dump(const struct s_schedule_e * const schent)
{
	if (!schent)
		return;

	filecfg_iprintf("entry {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("time {\n");
	filecfg_ilevel_inc();
	filecfg_iprintf("wday %d;\n", schent->time.wday);	// mandatory
	filecfg_iprintf("hour %d;\n", schent->time.hour);	// mandatory
	filecfg_iprintf("min %d;\n", schent->time.min);		// mandatory
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_iprintf("params {\n");
	filecfg_ilevel_inc();
	if (RM_UNKNOWN != schent->params.runmode)
		filecfg_dump_nodestr("runmode", filecfg_runmode_str(schent->params.runmode));
	if (RM_UNKNOWN != schent->params.dhwmode)
		filecfg_dump_nodestr("dhwmode", filecfg_runmode_str(schent->params.runmode));
	if (schent->params.legionella)
		filecfg_dump_nodebool("legionella", schent->params.legionella);
	if (schent->params.recycle)
		filecfg_dump_nodebool("recycle", schent->params.recycle);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump the full schedules to config file.
 * @return exec status
 * @warning not thread safe
 */
int filecfg_scheduler_dump(void)
{
	const struct s_schedule_e * schent, * schent_start;
	const struct s_schedule * sched;
	schedid_t id;

	filecfg_iprintf("scheduler {\n");
	filecfg_ilevel_inc();

	if (!Schedules.all)
		goto emptysched;

	for (id = 0; id < Schedules.lastid; id++) {
		sched = &Schedules.all[id];
		filecfg_iprintf("schedule \"%s\" {\n", sched->name);
		filecfg_ilevel_inc();

		schent_start = sched->head;
		scheduler_entry_dump(schent_start);
		for (schent = schent_start->next; schent && (schent_start != schent); schent = schent->next)
			scheduler_entry_dump(schent);

		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}


emptysched:
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
