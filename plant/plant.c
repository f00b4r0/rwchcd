//
//  plant/plant.c
//  rwchcd
//
//  (C) 2016-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation implementation.
 *
 * A "plant" is a collection of consummers, actuators and heatsources all related/connected to each other:
 * in a given plant, all the heatsources can provide heat to all of the plant's consummers.
 *
 * The plant implementation supports:
 * - Virtually unlimited number of heating circuits, DHWTs and actuators
 * - A single heatsource (but provision has been made in the code to support multiple heatsources)
 * - DHWT priority management
 * - Summer switchover for DHWT equipped with electric heating
 *
 * @bug no check on name lengths which can cause problems with storage/logging. See models for a fix.
 * @todo multiple heatsources: in switchover mode (e.g. wood furnace + fuel:
 * switch to fuel when wood dies out) and cascade mode (for large systems).
 */

#include <stdlib.h>	// calloc/free
#include <unistd.h>	// sleep
#include <assert.h>
#include <string.h>

#include "lib.h"
#include "pump.h"
#include "valve.h"
#include "hcircuit.h"
#include "dhwt.h"
#include "heatsource.h"
#include "plant.h"
#include "models.h"	// s_bmodel for plant_summer_ok()
#include "alarms.h"

/**
 * Find a pump by name in a plant.
 * @param plant the plant to find the pump from
 * @param name target name to find
 * @return pump if found, NULL otherwise
 */
struct s_pump * plant_fbn_pump(const struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_pump * restrict pump = NULL;
	plid_t id;

	if (!plant || !name)
		return (NULL);

	for (id = 0; id < plant->pumps.last; id++) {
		if (!strcmp(plant->pumps.all[id].name, name)) {
			pump = &plant->pumps.all[id];
			break;
		}
	}

	return (pump);
}

/**
 * Find a valve by name in a plant.
 * @param plant the plant to find the valve from
 * @param name target name to find
 * @return valve if found, NULL otherwise
 */
struct s_valve * plant_fbn_valve(const struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_valve * restrict valve = NULL;
	plid_t id;

	if (!plant || !name)
		return (NULL);

	for (id = 0; id < plant->valves.last; id++) {
		if (!strcmp(plant->valves.all[id].name, name)) {
			valve = &plant->valves.all[id];
			break;
		}
	}

	return (valve);
}

/**
 * Find an hcircuit by name in a plant.
 * @param plant the plant to find the hcircuit from
 * @param name target name to find
 * @return hcircuit if found, NULL otherwise
 */
struct s_hcircuit * plant_fbn_hcircuit(const struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_hcircuit * restrict hcircuit = NULL;
	plid_t id;

	if (!plant || !name)
		return (NULL);

	for (id = 0; id < plant->hcircuits.last; id++) {
		if (!strcmp(plant->hcircuits.all[id].name, name)) {
			hcircuit = &plant->hcircuits.all[id];
			break;
		}
	}

	return (hcircuit);
}

/**
 * Find a dhwt by name in a plant.
 * @param plant the plant to find the dhwt from
 * @param name target name to find
 * @return dhwt if found, NULL otherwise
 */
struct s_dhwt * plant_fbn_dhwt(const struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_dhwt * restrict dhwt = NULL;
	plid_t id;

	if (!plant || !name)
		return (NULL);

	for (id = 0; id < plant->dhwts.last; id++) {
		if (!strcmp(plant->dhwts.all[id].name, name)) {
			dhwt = &plant->dhwts.all[id];
			break;
		}
	}

	return (dhwt);
}

/**
 * Find a heatsource by name in a plant.
 * @param plant the plant to find the heatsource from
 * @param name target name to find
 * @return heatsource if found, NULL otherwise
 */
struct s_heatsource * plant_fbn_heatsource(const struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_heatsource * restrict source = NULL;
	plid_t id;

	if (!plant || !name)
		return (NULL);

	for (id = 0; id < plant->heatsources.last; id++) {
		if (!strcmp(plant->heatsources.all[id].name, name)) {
			source = &plant->heatsources.all[id];
			break;
		}
	}

	return (source);
}

/**
 * Create a new plant.
 * @return newly created pointer or NULL if failed
 */
struct s_plant * plant_new(void)
{
	struct s_plant * const plant = calloc(1, sizeof(*plant));

	return (plant);
}

/**
 * Delete a plant.
 * Turn everything off, deallocate all resources and free pointer
 * @param plant the plant to destroy
 */
void plant_del(struct s_plant * plant)
{
	plid_t id;
	
	if (!plant)
		return;

	// clear all registered pumps
	for (id = 0; id < plant->pumps.last; id++)
		pump_cleanup(&plant->pumps.all[id]);
	plant->pumps.last = 0;
	plant->pumps.n = 0;
	free(plant->pumps.all);

	// clear all registered valves
	for (id = 0; id < plant->valves.last; id++)
		valve_cleanup(&plant->valves.all[id]);
	plant->valves.last = 0;
	plant->valves.n = 0;
	free(plant->valves.all);

	// clear all registered circuits
	for (id = 0; id < plant->hcircuits.last; id++)
		hcircuit_cleanup(&plant->hcircuits.all[id]);
	plant->hcircuits.last = 0;
	plant->hcircuits.n = 0;
	free(plant->hcircuits.all);

	// clear all registered dhwt
	for (id = 0; id < plant->dhwts.last; id++)
		dhwt_cleanup(&plant->dhwts.all[id]);
	plant->dhwts.last = 0;
	plant->dhwts.n = 0;
	free(plant->dhwts.all);

	// clear all registered heatsources
	for (id = 0; id < plant->heatsources.last; id++)
		heatsource_cleanup(&plant->heatsources.all[id]);
	plant->heatsources.last = 0;
	plant->heatsources.n = 0;
	free(plant->heatsources.all);

	free(plant);
}

/** Plant devices identifiers */
enum e_plant_devtype {
	PDEV_PUMP,	///< pump
	PDEV_VALVE,	///< valve
	PDEV_HEATS,	///< heatsource
	PDEV_HCIRC,	///< heating circuit
	PDEV_DHWT,	///< dhwt
};

/**
 * Generic error logger for online/offline operations.
 * @param errorn the error value
 * @param devid the plant device id
 * @param devname the plant device name
 * @param pdev the plant device type identifier
 */
static void plant_onfline_printerr(const enum e_execs errorn, const int devid, const char * restrict devname, const enum e_plant_devtype pdev, bool on)
{
	const char * restrict devtype;

	if (ALL_OK == errorn)
		return;

	switch (pdev) {
		case PDEV_PUMP:
			devtype = _("pump");
			break;
		case PDEV_VALVE:
			devtype = _("valve");
			break;
		case PDEV_HEATS:
			devtype = _("heatsource");
			break;
		case PDEV_HCIRC:
			devtype = _("heating circuit");
			break;
		case PDEV_DHWT:
			devtype = _("DHWT");
			break;
		default:
			devtype = "";
			break;
	}

	pr_err(_("Failure to bring %s %d (\"%s\") %sline:"), devtype, devid, devname, on ? "on" : "off");
	switch (-errorn) {
		case ESENSORINVAL:
		case ESENSORSHORT:
		case ESENSORDISCON:	// sensor issues
			pr_err(_("Mandatory sensor failure (%d)."), errorn);
			break;
		case ENOTCONFIGURED:
			pr_err(_("Unconfigured %s."), devtype);
			break;
		case EMISCONFIGURED:
			pr_err(_("Misconfigured %s."), devtype);
			break;
		case ENOTIMPLEMENTED:
			pr_err(_("Setting not implemented."));
			break;
		default:
			pr_err(_("Unknown error (%d)"), errorn);
			break;
	}

}

/**
 * Bring plant online.
 * BY design this function will try to bring online as many plant devices
 * as possible (errors are reported but will not stop the process).
 * @param plant target plant
 * @return exec status (-EGENERIC if any sub call returned an error)
 * @note REQUIRES valid sensor values before being called
 * @todo error handling
 */
int plant_online(struct s_plant * restrict const plant)
{
	struct s_pump * pump;
	struct s_valve * valve;
	struct s_hcircuit * hcircuit;
	struct s_dhwt * dhwt;
	struct s_heatsource * heatsource;
	bool suberror = false;
	plid_t id;
	int ret;

	if (!plant)
		return (-EINVALID);

	if (!plant->set.configured)
		return (-ENOTCONFIGURED);

	//plant_restore(plant);

	// online the actuators first
	// pumps
	for (id = 0; id < plant->pumps.last; id++) {
		pump = &plant->pumps.all[id];
		ret = pump_online(pump);
		pump->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, pump->name, PDEV_PUMP, true);
			pump_offline(pump);
			suberror = true;
		}
	}

	// valves
	for (id = 0; id < plant->valves.last; id++) {
		valve = &plant->valves.all[id];
		ret = valve_online(valve);
		valve->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, valve->name, PDEV_VALVE, true);
			valve_offline(valve);
			suberror = true;
		}
	}
	
	// next deal with the consummers
	// hcircuits first
	for (id = 0; id < plant->hcircuits.last; id++) {
		hcircuit = &plant->hcircuits.all[id];
		ret = hcircuit_online(hcircuit);
		hcircuit->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, hcircuit->name, PDEV_HCIRC, true);
			hcircuit_offline(hcircuit);
			suberror = true;
		}
	}

	// then dhwt
	for (id = 0; id < plant->dhwts.last; id++) {
		dhwt = &plant->dhwts.all[id];
		ret = dhwt_online(dhwt);
		dhwt->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, dhwt->name, PDEV_DHWT, true);
			dhwt_offline(dhwt);
			suberror = true;
		}
		else {
			// find largest DHWT prio value
			if (dhwt->set.prio > plant->run.dhwt_maxprio)
				plant->run.dhwt_maxprio = dhwt->set.prio;
		}
	}

	// finally online the heat sources
	for (id = 0; id < plant->heatsources.last; id++) {
		heatsource = &plant->heatsources.all[id];
		ret = heatsource_online(heatsource);
		heatsource->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, heatsource->name, PDEV_HEATS, true);
			heatsource_offline(heatsource);
			suberror = true;
		}
	}

	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else {
		plant->run.online = true;
		return (ALL_OK);
	}
}

/**
 * Take plant offline.
 * @param plant target plant
 * @return exec status (-EGENERIC if any sub call returned an error)
 * @todo error handling
 */
int plant_offline(struct s_plant * restrict const plant)
{
	struct s_pump * pump;
	struct s_valve * valve;
	struct s_hcircuit * hcircuit;
	struct s_dhwt * dhwt;
	struct s_heatsource * heatsource;
	bool suberror = false;
	plid_t id;
	int ret;
	
	if (!plant)
		return (-EINVALID);
	
	if (!plant->set.configured)
		return (-ENOTCONFIGURED);

	//plant_save(plant);
	
	// offline the consummers first
	// circuits first
	for (id = 0; id < plant->hcircuits.last; id++) {
		hcircuit = &plant->hcircuits.all[id];
		ret = hcircuit_offline(hcircuit);
		hcircuit->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, hcircuit->name, PDEV_HCIRC, false);
			suberror = true;
		}
	}
	
	// then dhwt
	for (id = 0; id < plant->dhwts.last; id++) {
		dhwt = &plant->dhwts.all[id];
		ret = dhwt_offline(dhwt);
		dhwt->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, dhwt->name, PDEV_DHWT, false);
			suberror = true;
		}
	}
	
	// next deal with the heat sources
	for (id = 0; id < plant->heatsources.last; id++) {
		heatsource = &plant->heatsources.all[id];
		ret = heatsource_offline(heatsource);
		heatsource->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, heatsource->name, PDEV_HEATS, false);
			suberror = true;
		}
	}
	
	// finally offline the actuators
	// valves
	for (id = 0; id < plant->valves.last; id++) {
		valve = &plant->valves.all[id];
		ret = valve_offline(valve);
		valve->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, valve->name, PDEV_VALVE, false);
			suberror = true;
		}
	}
	
	// pumps
	for (id = 0; id < plant->pumps.last; id++) {
		pump = &plant->pumps.all[id];
		ret = pump_offline(pump);
		pump->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, id, pump->name, PDEV_PUMP, false);
			suberror = true;
		}
	}

	memset(&plant->run, 0x0, sizeof(plant->run));
	memset(&plant->pdata.run, 0x0, sizeof(plant->pdata.run));
	
	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}

/**
 * Raise an alarm for a plant device.
 * Generic implementation of a plant-wide error message handler using the alarms subsystem.
 * @param errorn the error value
 * @param devid the plant device id
 * @param devname the plant device name
 * @param pdev the plant device type identifier
 * @return exec status
 */
static int plant_alarm(const enum e_execs errorn, const int devid, const char * restrict devname, const enum e_plant_devtype pdev)
{
	const char * restrict const devdesigf = "%s #%d (\"%s\")";
	const char * restrict const msglcd = _("Plant alarm!");
	const char * restrict msgf = NULL, * restrict devtype;
	char * restrict devdesig = NULL, * restrict msg = NULL;
	size_t size;
	int ret;

	switch (pdev) {
		case PDEV_PUMP:
			devtype = _("pump");
			break;
		case PDEV_VALVE:
			devtype = _("valve");
			break;
		case PDEV_HEATS:
			devtype = _("heatsource");
			break;
		case PDEV_HCIRC:
			devtype = _("heating circuit");
			break;
		case PDEV_DHWT:
			devtype = _("DHWT");
			break;
		default:
			devtype = "";
			break;
	}

	snprintf_automalloc(devdesig, size, devdesigf, devtype, devid, devname);
	if (!devdesig)
		return (-EOOM);

	switch (-errorn) {
		case ALL_OK:
			break;
		default:
			msgf = _("Unknown error (%d) on %s");
			snprintf_automalloc(msg, size, msgf, errorn, devdesig);
			goto msgset;
		case ESAFETY:
			msgf = _("SAFETY CRITICAL ERROR ON %s!");
			break;
		case EINVALIDMODE:
			msgf = _("Invalid mode set on %s");
			break;
		case ESENSORINVAL:
		case ESENSORSHORT:
		case ESENSORDISCON:	// XXX review, sensor alarms currently detailed in backend
			msgf = _("Sensor problem on %s");
			break;
		case ENOTCONFIGURED:	// this really should not happen
			msgf = _("%s is not configured!");
			break;
		case EMISCONFIGURED:	// this really should not happen
			msgf = _("%s is misconfigured!");
			break;
		case EOFFLINE:		// this really should not happen
			msgf = _("%s is offline!");
			break;
	}

	if (!msg)	// handle common switch cases once
		snprintf_automalloc(msg, size, msgf, devdesig);

msgset:
	ret = alarms_raise(errorn, msg, msglcd);

	free(msg);
	free(devdesig);

	return (ret);
}

/**
 * Collect heat requests from a plant.
 * This function collects heat requests from consummers (hcircuits and dhwts),
 * updates the plant_could_sleep flag and current plant-wide DHWT priority,
 * and collects active DHWT charge priority strategies.
 * @note Because we OR the charge priorities from all active DHWTs, care must be taken handling these signals.
 * @param plant target plant
 */
static void plant_collect_hrequests(struct s_plant * restrict const plant)
{
	const timekeep_t now = timekeep_now();
	const struct s_hcircuit * hcircuit;
	const struct s_dhwt * restrict dhwt;
	temp_t temp, temp_request = RWCHCD_TEMP_NOREQUEST, temp_req_dhw = RWCHCD_TEMP_NOREQUEST;
	bool dhwt_absolute = false, dhwt_sliding = false, dhwt_reqdhw = false, dhwt_charge = false;
	plid_t id;

	assert(plant);
	assert(plant->run.online);

	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (id = 0; id < plant->hcircuits.last; id++) {
		hcircuit = &plant->hcircuits.all[id];
		if (!hcircuit->run.online || (ALL_OK != hcircuit->status))
			continue;

		temp = aler(&hcircuit->run.heat_request);
		temp_request = (temp > temp_request) ? temp : temp_request;
		if (RWCHCD_TEMP_NOREQUEST != temp)
			plant->run.last_creqtime = now;
	}

	// check if last request exceeds timeout, or if last_creqtime is unset (happens at startup)
	if (!plant->run.last_creqtime || ((now - plant->run.last_creqtime) > plant->set.sleeping_delay))
		plant->pdata.run.plant_could_sleep = true;
	else
		plant->pdata.run.plant_could_sleep = false;

	/// XXX @todo should update PCS if any DHWT is active and cannot do electric?

	// then dhwt
	for (id = 0; id < plant->dhwts.last; id++) {
		dhwt = &plant->dhwts.all[id];
		if (!dhwt->run.online || (ALL_OK != dhwt->status))
			continue;

		temp = dhwt->run.heat_request;
		temp_req_dhw = (temp > temp_req_dhw) ? temp : temp_req_dhw;

		// handle DHW charge priority (only in non-electric mode)
		if (aler(&dhwt->run.charge_on) && !aler(&dhwt->run.electric_mode)) {
			dhwt_charge = true;
			switch (dhwt->set.dhwt_cprio) {
				case DHWTP_SLIDDHW:
					dhwt_reqdhw = true;
					// fallthrough
				case DHWTP_SLIDMAX:
					dhwt_sliding = true;
					break;
				case DHWTP_ABSOLUTE:
					dhwt_absolute = true;
					// fallthrough
				case DHWTP_PARALDHW:
					dhwt_reqdhw = true;
				case DHWTP_PARALMAX:
				default:
					/* nothing */
					break;
			}

			// make sure that plant-wide DHWT priority is always set to the current highest bidder
			if (dhwt->set.prio < plant->pdata.run.dhwt_currprio)
				plant->pdata.run.dhwt_currprio = dhwt->set.prio;
		}
	}

	// if no heatsource-based DHWT charge is in progress, increase prio threshold (up to max)
	if (!dhwt_charge && (plant->pdata.run.dhwt_currprio < plant->run.dhwt_maxprio))
		plant->pdata.run.dhwt_currprio++;

	/*
	 if dhwt_absolute => circuits don't receive heat
	 if dhwt_sliding => circuits can be reduced
	 if dhwt_reqdhw => heat request = max dhw request, else max (max circuit, max dhw)
	 */

	// calculate max of circuit requests and dhwt requests
	temp_request = (temp_req_dhw > temp_request) ? temp_req_dhw : temp_request;

	// select effective heat request
	plant->run.plant_hrequest = dhwt_reqdhw ? temp_req_dhw : temp_request;

	plant->pdata.run.dhwc_absolute = dhwt_absolute;
	plant->pdata.run.dhwc_sliding = dhwt_sliding;
}

/**
 * Dispatch heat requests from a plant.
 * @warning currently supports single heat source, all consummers connected to it
 * @param plant target plant
 * @todo XXX logic for multiple heatsources (cascade and/or failover)
 */
static void plant_dispatch_hrequests(struct s_plant * restrict const plant)
{
	struct s_heatsource * heatsource;
	bool serviced = false;
	plid_t id;

	assert(plant);
	assert(plant->run.online);

	assert(plant->heatsources.last <= 1);	// XXX TODO: only one source supported at the moment
	for (id = 0; id < plant->heatsources.last; id++) {
		heatsource = &plant->heatsources.all[id];
		if (!heatsource->run.online)
			continue;

		// XXX function call?
		heatsource->run.temp_request = plant->run.plant_hrequest;
		serviced = true;
	}

	if (!serviced)
		alarms_raise(-EEMPTY, _("No heatsource available!"), _("NO HEATSRC AVAIL"));
}

/**
 * Check if a plant can enter summer mode.
 * Parse all the plant's circuits' building models for summer switch evaluation. Conditions:
 * - If @b ALL online bmodels are compatible with summer mode, summer mode is set.
 * - If @b ANY online bmodel is incompatible with summer mode, summer mode is unset.
 * @param plant target plant
 * @return summer mode
 */
static bool plant_summer_ok(const struct s_plant * restrict const plant)
{
	const struct s_hcircuit * restrict hcircuit;
	bool summer = true;
	plid_t id;

	assert(plant);
	assert(plant->run.online);

	for (id = 0; id < plant->hcircuits.last; id++) {
		hcircuit = &plant->hcircuits.all[id];
		if (!hcircuit->run.online)
			continue;
		summer &= aler(&hcircuit->set.p.bmodel->run.summer);
	}

	return (summer);
}

/**
 * Plant summer maintenance operations.
 * When summer conditions are met, the pumps and mixing valves are periodically actuated.
 * The idea of this function is to run as an override filter in the plant_run()
 * loop so that during summer maintenance, the state of these actuators is
 * overriden.
 * @param plant target plant
 * @return exec status
 * @todo sequential run (instead of parallel), then we can handle isolation valves
 */
static int plant_summer_maintenance(struct s_plant * restrict const plant)
{
	const timekeep_t now = timekeep_now();
	struct s_pump * pump;
	struct s_valve * valve;
	plid_t id;
	int ret;

	assert(plant);
	assert(plant->run.online);

	// coherent config is ensured during config parsing
	assert(plant->set.summer_run_interval && plant->set.summer_run_duration);

	// don't do anything if summer AND plant asleep aren't in effect
	if (!(plant_summer_ok(plant) && plant->pdata.run.plant_could_sleep)) {
		plant->run.summer_timer = now;
		return (ALL_OK);
	}

	// stop running when duration is exceeded (this also prevents running when summer is first triggered)
	if ((now - plant->run.summer_timer) >= (plant->set.summer_run_interval + plant->set.summer_run_duration)) {
		pr_log(_("Summer maintenance completed"));
		plant->run.summer_timer = now;
	}

	// don't run too often
	if ((now - plant->run.summer_timer) < plant->set.summer_run_interval)
		return (ALL_OK);

	dbgmsg(1, 1, "summer maintenance active");

	// open all valves
	for (id = 0; id < plant->valves.last; id++) {
		valve = &plant->valves.all[id];
		if (!valve->run.online)
			continue;

		if (VA_TYPE_ISOL == valve->set.type)
			continue;	// don't touch isolation valves

		if (valve->run.dwht_use)
			continue;	// don't touch DHWT valves when in use

		ret = valve_reqopen_full(valve);

		if (ALL_OK != ret)
			dbgerr("valve_reqopen_full failed on %d (%d)", id, ret);
	}

	// set all pumps ON
	for (id = 0; id < plant->pumps.last; id++) {
		pump = &plant->pumps.all[id];
		if (!pump->run.online)
			continue;

		if (pump->run.dwht_use)
			continue;	// don't touch DHWT pumps when in use

		ret = pump_set_state(pump, ON, NOFORCE);

		if (ALL_OK != ret)
			dbgerr("pump_set_state failed on %d (%d)", id, ret);
	}

	return (ALL_OK);
}

/**
 * Run the plant.
 * This function operates all plant elements in turn by enumerating through each list.
 * @param plant the target plant to run
 * @return exec status (-EGENERIC if any sub call returned an error)
 */
int plant_run(struct s_plant * restrict const plant)
{
	struct s_hcircuit * hcircuit;
	struct s_dhwt * dhwt;
	struct s_heatsource * heatsource;
	struct s_valve * valve;
	struct s_pump * pump;
	bool overtemp = false, suberror = false;
	timekeep_t stop_delay = 0;
	plid_t id;

	if (unlikely(!plant))
		return (-EINVALID);
	
	if (unlikely(!plant->run.online))
		return (-EOFFLINE);

	// run the consummers first so they can set their requested heat input
	// dhwt first
	for (id = 0; id < plant->dhwts.last; id++) {
		dhwt = &plant->dhwts.all[id];
		dhwt->status = dhwt_run(dhwt);

		switch (-dhwt->status) {
			case ALL_OK:
				break;
			default:
				dhwt_offline(dhwt);			// something really bad happened
				// fallthrough
			case EINVALIDMODE:
				dhwt->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
				// fallthrough
			case ESENSORINVAL:
			case ESENSORSHORT:
			case ESENSORDISCON:	// sensor issues are handled by dhwt_run()
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(dhwt->status, id, dhwt->name, PDEV_DHWT);
				continue;
		}
	}

	// then circuits
	for (id = 0; id < plant->hcircuits.last; id++) {
		hcircuit = &plant->hcircuits.all[id];
		hcircuit->status = hcircuit_run(hcircuit);

		switch (-hcircuit->status) {
			case ALL_OK:
				break;
			default:
				hcircuit_offline(hcircuit);		// something really bad happened
				// fallthrough
			case EINVALIDMODE:
				hcircuit->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
				// fallthrough
			case ESENSORINVAL:
			case ESENSORSHORT:
			case ESENSORDISCON:	// sensor issues are handled by hcircuit_run()
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(hcircuit->status, id, hcircuit->name, PDEV_HCIRC);
				continue;
		}
	}

	// collect and dispatch heat requests
	plant_collect_hrequests(plant);
	plant_dispatch_hrequests(plant);

	// now run the heat sources
	for (id = 0; id < plant->heatsources.last; id++) {
		heatsource = &plant->heatsources.all[id];
		heatsource->status = heatsource_run(heatsource);

		// always update overtemp (which can be triggered with -ESAFETY)
		overtemp |= aler(&heatsource->run.overtemp);

		if (unlikely(ALL_OK != heatsource->status)) {
			suberror = true;
			plant_alarm(heatsource->status, id, heatsource->name, PDEV_HEATS);
			continue;	// no further processing for this source
		}

		// max stop delay
		stop_delay = (heatsource->run.target_consumer_sdelay > stop_delay) ? heatsource->run.target_consumer_sdelay : stop_delay;

		// XXX consumer_shift: if a critical shift is in effect it overrides the non-critical one
		assert(plant->heatsources.last <= 1);	// XXX TODO: only one source supported at the moment for consummer_shift
		plant->pdata.run.consumer_shift = heatsource->run.cshift_crit ? heatsource->run.cshift_crit : heatsource->run.cshift_noncrit;
	}

	// reflect global stop delay and overtemp
	plant->pdata.run.consumer_sdelay = stop_delay;
	plant->pdata.run.hs_overtemp = overtemp;
	if (overtemp) {
		plant->pdata.run.plant_could_sleep = false;	// disable during overtemp
		plant->pdata.run.consumer_shift = RWCHCD_CSHIFT_MAX;
	}

	if (plant->set.summer_maintenance)
		plant_summer_maintenance(plant);

	// finally run the actuators
	// run the valves
	for (id = 0; id < plant->valves.last; id++) {
		valve = &plant->valves.all[id];
		valve->status = valve_run(valve);

		if (unlikely(ALL_OK != valve->status)) {
			suberror = true;
			plant_alarm(valve->status, id, valve->name, PDEV_VALVE);
			continue;	// no further processing for this valve
		}
	}
	
	// run the pumps
	for (id = 0; id < plant->pumps.last; id++) {
		pump = &plant->pumps.all[id];
		pump->status = pump_run(pump);

		if (unlikely(ALL_OK != pump->status)) {
			suberror = true;
			plant_alarm(pump->status, id, pump->name, PDEV_PUMP);
			continue;	// no further processing for this pump
		}
	}

	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}
