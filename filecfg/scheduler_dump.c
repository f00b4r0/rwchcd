//
//  filecfg/scheduler_dump.c
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
#include "filecfg.h"

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
		filecfg_iprintf("runmode \"%s\";\n", filecfg_runmode_str(schent->params.runmode));
	if (RM_UNKNOWN != schent->params.dhwmode)
		filecfg_iprintf("dhwmode \"%s\";\n", filecfg_runmode_str(schent->params.runmode));
	if (schent->params.legionella)
		filecfg_iprintf("legionella %s;\n", filecfg_bool_str(schent->params.legionella));
	if (schent->params.recycle)
		filecfg_iprintf("recycle %s;\n", filecfg_bool_str(schent->params.recycle));
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

	filecfg_iprintf("scheduler {\n");
	filecfg_ilevel_inc();

	if (!Schedules.schead)
		goto emptysched;

	for (sched = Schedules.schead; sched; sched = sched->next) {
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
