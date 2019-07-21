//
//  plant.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
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
 * @todo multiple heatsources: in switchover mode (e.g. wood furnace + fuel:
 * switch to fuel when wood dies out) and cascade mode (for large systems).
 */

#include <stdlib.h>	// calloc/free
#include <unistd.h>	// sleep
#include <assert.h>
#include <string.h>

#include "config.h"
#include "runtime.h"	// for runtime_get()->config
#include "lib.h"
#include "plant.h"
#include "pump.h"
#include "valve.h"
#include "hcircuit.h"
#include "dhwt.h"
#include "heatsource.h"
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
	const struct s_pump_l * restrict pumpl;
	struct s_pump * restrict pump = NULL;

	if (!plant || !name)
		return (NULL);

	for (pumpl = plant->pump_head; pumpl; pumpl = pumpl->next) {
		if (!strcmp(pumpl->pump->name, name)) {
			pump = pumpl->pump;
			break;
		}
	}

	return (pump);
}

/**
 * Create a new pump and attach it to the plant.
 * @param plant the plant to attach the pump to
 * @param name @b UNIQUE pump name. A local copy is created
 * @return pointer to the created pump
 */
struct s_pump * plant_new_pump(struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_pump * restrict pump = NULL;
	struct s_pump_l * restrict pumpelmt = NULL;
	char * restrict str = NULL;

	if (!plant || !name)
		goto fail;

	// deal with name
	// ensure unique name
	if (plant_fbn_pump(plant, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new pump
	pump = pump_new();
	if (!pump)
		goto fail;

	// set name
	pump->name = str;

	// create pump element
	pumpelmt = calloc(1, sizeof(*pumpelmt));
	if (!pumpelmt)
		goto fail;
	
	// attach created pump to element
	pumpelmt->pump = pump;
	
	// attach it to the plant
	pumpelmt->id = plant->pump_n;
	pumpelmt->next = plant->pump_head;
	plant->pump_head = pumpelmt;
	plant->pump_n++;
	
	return (pump);
	
fail:
	free(str);
	free(pump);
	free(pumpelmt);
	return (NULL);
}

/**
 * Find a valve by name in a plant.
 * @param plant the plant to find the valve from
 * @param name target name to find
 * @return valve if found, NULL otherwise
 */
struct s_valve * plant_fbn_valve(const struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_valve_l * restrict valvel;
	struct s_valve * restrict valve = NULL;

	if (!plant || !name)
		return (NULL);

	for (valvel = plant->valve_head; valvel; valvel = valvel->next) {
		if (!strcmp(valvel->valve->name, name)) {
			valve = valvel->valve;
			break;
		}
	}

	return (valve);
}

/**
 * Create a new valve and attach it to the plant.
 * @param plant the plant to attach the valve to
 * @param name @b UNIQUE valve name. A local copy is created
 * @return pointer to the created valve
 */
struct s_valve * plant_new_valve(struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_valve * restrict valve = NULL;
	struct s_valve_l * restrict valveelmt = NULL;
	char * restrict str = NULL;

	if (!plant || !name)
		goto fail;

	// deal with name
	// ensure unique name
	if (plant_fbn_valve(plant, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new valve
	valve = valve_new();
	if (!valve)
		goto fail;

	// set name
	valve->name = str;
	
	// create valve element
	valveelmt = calloc(1, sizeof(*valveelmt));
	if (!valveelmt)
		goto fail;
	
	// attach created valve to element
	valveelmt->valve = valve;
	
	// attach it to the plant
	valveelmt->id = plant->valve_n;
	valveelmt->next = plant->valve_head;
	plant->valve_head = valveelmt;
	plant->valve_n++;
	
	return (valve);
	
fail:
	free(str);
	free(valve);
	free(valveelmt);
	return (NULL);
}

/**
 * Find a circuit by name in a plant.
 * @param plant the plant to find the circuit from
 * @param name target name to find
 * @return circuit if found, NULL otherwise
 */
struct s_hcircuit * plant_fbn_circuit(const struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_heating_circuit_l * restrict circuitl;
	struct s_hcircuit * restrict circuit = NULL;

	if (!plant || !name)
		return (NULL);

	for (circuitl = plant->circuit_head; circuitl; circuitl = circuitl->next) {
		if (!strcmp(circuitl->circuit->name, name)) {
			circuit = circuitl->circuit;
			break;
		}
	}

	return (circuit);
}

/**
 * Create a new heating circuit and attach it to the plant.
 * @param plant the plant to attach the circuit to
 * @param name @b UNIQUE circuit name. A local copy is created
 * @return pointer to the created heating circuit
 */
struct s_hcircuit * plant_new_circuit(struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_hcircuit * restrict circuit = NULL;
	struct s_heating_circuit_l * restrict circuitelement = NULL;
	char * restrict str = NULL;

	if (!plant || !name)
		goto fail;

	// deal with name
	// ensure unique name
	if (plant_fbn_circuit(plant, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new circuit
	circuit = hcircuit_new();
	if (!circuit)
		goto fail;

	// set name
	circuit->name = str;

	// set plant data
	circuit->pdata = &plant->pdata;

	// create a new circuit element
	circuitelement = calloc(1, sizeof(*circuitelement));
	if (!circuitelement)
		goto fail;

	// attach the created circuit to the element
	circuitelement->circuit = circuit;

	// attach it to the plant
	circuitelement->id = plant->circuit_n;
	circuitelement->next = plant->circuit_head;
	plant->circuit_head = circuitelement;
	plant->circuit_n++;

	return (circuit);

fail:
	free(str);
	free(circuit);
	free(circuitelement);
	return (NULL);
}

/**
 * Find a dhwt by name in a plant.
 * @param plant the plant to find the dhwt from
 * @param name target name to find
 * @return dhwt if found, NULL otherwise
 */
struct s_dhw_tank * plant_fbn_dhwt(const struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_dhw_tank_l * restrict dhwtl;
	struct s_dhw_tank * restrict dhwt = NULL;

	if (!plant || !name)
		return (NULL);

	for (dhwtl = plant->dhwt_head; dhwtl; dhwtl = dhwtl->next) {
		if (!strcmp(dhwtl->dhwt->name, name)) {
			dhwt = dhwtl->dhwt;
			break;
		}
	}

	return (dhwt);
}

/**
 * Create a new dhw tank and attach it to the plant.
 * @param plant the plant to attach the tank to
 * @param name @b UNIQUE dhwt name. A local copy is created
 * @return pointer to the created tank
 */
struct s_dhw_tank * plant_new_dhwt(struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_dhw_tank * restrict dhwt = NULL;
	struct s_dhw_tank_l * restrict dhwtelement = NULL;
	char * restrict str = NULL;

	if (!plant || !name)
		goto fail;

	// deal with name
	// ensure unique name
	if (plant_fbn_dhwt(plant, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new tank
	dhwt = dhwt_new();
	if (!dhwt)
		goto fail;

	// set name
	dhwt->name = str;

	// set plant data
	dhwt->pdata = &plant->pdata;

	// create a new tank element
	dhwtelement = calloc(1, sizeof(*dhwtelement));
	if (!dhwtelement)
		goto fail;

	// attach the created tank to the element
	dhwtelement->dhwt = dhwt;

	// attach it to the plant
	dhwtelement->id = plant->dhwt_n;
	dhwtelement->next = plant->dhwt_head;
	plant->dhwt_head = dhwtelement;
	plant->dhwt_n++;

	return (dhwt);

fail:
	free(str);
	free(dhwt);
	free(dhwtelement);
	return (NULL);
}

/**
 * Find a heatsource by name in a plant.
 * @param plant the plant to find the heatsource from
 * @param name target name to find
 * @return heatsource if found, NULL otherwise
 */
struct s_heatsource * plant_fbn_heatsource(const struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_heatsource_l * restrict sourcel;
	struct s_heatsource * restrict source = NULL;

	if (!plant || !name)
		return (NULL);

	for (sourcel = plant->heats_head; sourcel; sourcel = sourcel->next) {
		if (!strcmp(sourcel->heats->name, name)) {
			source = sourcel->heats;
			break;
		}
	}

	return (source);
}

/**
 * Create a new heatsource in the plant.
 * @param plant the target plant
 * @param name @b UNIQUE heatsource name. A local copy is created
 * @return pointer to the created source
 */
struct s_heatsource * plant_new_heatsource(struct s_plant * restrict const plant, const char * restrict const name)
{
	struct s_heatsource * restrict source = NULL;
	struct s_heatsource_l * restrict sourceelement = NULL;
	char * restrict str = NULL;

	if (!plant || !name)
		goto fail;

	// deal with name
	// ensure unique name
	if (plant_fbn_heatsource(plant, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new source
	source = heatsource_new();
	if (!source)
		goto fail;

	// set name
	source->name = str;

	// set plant data
	source->pdata = &plant->pdata;

	// create a new source element
	sourceelement = calloc(1, sizeof(*sourceelement));
	if (!sourceelement)
		goto fail;

	// attach the created source to the element
	sourceelement->heats = source;

	// attach it to the plant
	sourceelement->id = plant->heats_n;
	sourceelement->next = plant->heats_head;
	plant->heats_head = sourceelement;
	plant->heats_n++;

	return (source);

fail:
	free(str);
	if (source && source->cb.del_priv)
		source->cb.del_priv(source->priv);
	free(source);
	free(sourceelement);
	return (NULL);
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
	struct s_pump_l * pumpelmt, * pumpnext;
	struct s_valve_l * valveelmt, * valvenext;
	struct s_heating_circuit_l * circuitelement, * circuitlnext;
	struct s_dhw_tank_l * dhwtelement, * dhwtlnext;
	struct s_heatsource_l * sourceelement, * sourcenext;
	
	if (!plant)
		return;

	// clear all registered pumps
	pumpelmt = plant->pump_head;
	while (pumpelmt) {
		pumpnext = pumpelmt->next;
		pump_del(pumpelmt->pump);
		free(pumpelmt);
		plant->pump_n--;
		pumpelmt = pumpnext;
	}
	
	// clear all registered valves
	valveelmt = plant->valve_head;
	while (valveelmt) {
		valvenext = valveelmt->next;
		valve_del(valveelmt->valve);
		free(valveelmt);
		plant->valve_n--;
		valveelmt = valvenext;
	}
	
	// clear all registered circuits
	circuitelement = plant->circuit_head;
	while (circuitelement) {
		circuitlnext = circuitelement->next;
		hcircuit_del(circuitelement->circuit);
		free(circuitelement);
		plant->circuit_n--;
		circuitelement = circuitlnext;
	}

	// clear all registered dhwt
	dhwtelement = plant->dhwt_head;
	while (dhwtelement) {
		dhwtlnext = dhwtelement->next;
		dhwt_del(dhwtelement->dhwt);
		free(dhwtelement);
		plant->dhwt_n--;
		dhwtelement = dhwtlnext;
	}

	// clear all registered heatsources
	sourceelement = plant->heats_head;
	while (sourceelement) {
		sourcenext = sourceelement->next;
		heatsource_del(sourceelement->heats);
		free(sourceelement);
		plant->heats_n--;
		sourceelement = sourcenext;
	}

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
		case -ESENSORINVAL:
		case -ESENSORSHORT:
		case -ESENSORDISCON:	// sensor issues
			pr_err(_("Mandatory sensor failure (%d)."), errorn);
			break;
		case -ENOTCONFIGURED:
			pr_err(_("Unconfigured %s."), devtype);
			break;
		case -EMISCONFIGURED:
			pr_err(_("Misconfigured %s."), devtype);
			break;
		case -ENOTIMPLEMENTED:
			pr_err(_("Setting not implemented."));
			break;
		case ENOTCONFIGURED:
			pr_err(_("Unconfigured %s."), devtype);
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
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	bool suberror = false;
	int ret;

	if (!plant)
		return (-EINVALID);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	//plant_restore(plant);

	// online the actuators first
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_online(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, pumpl->id, pumpl->pump->name, PDEV_PUMP, true);
			pump_offline(pumpl->pump);
			suberror = true;
		}
	}

	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_online(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, valvel->id, valvel->valve->name, PDEV_VALVE, true);
			valve_offline(valvel->valve);
			suberror = true;
		}
	}
	
	// next deal with the consummers
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = hcircuit_online(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, circuitl->id, circuitl->circuit->name, PDEV_HCIRC, true);
			hcircuit_offline(circuitl->circuit);
			suberror = true;
		}
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, dhwtl->id, dhwtl->dhwt->name, PDEV_DHWT, true);
			dhwt_offline(dhwtl->dhwt);
			suberror = true;
		}
		else {
			// find largest DHWT prio value
			if (dhwtl->dhwt->set.prio > plant->run.dhwt_maxprio)
				plant->run.dhwt_maxprio = dhwtl->dhwt->set.prio;
		}
	}

	// finally online the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_online(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, heatsourcel->id, heatsourcel->heats->name, PDEV_HEATS, true);
			heatsource_offline(heatsourcel->heats);
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
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	bool suberror = false;
	int ret;
	
	if (!plant)
		return (-EINVALID);
	
	if (!plant->configured)
		return (-ENOTCONFIGURED);

	//plant_save(plant);
	
	// offline the consummers first
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = hcircuit_offline(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, circuitl->id, circuitl->circuit->name, PDEV_HCIRC, false);
			suberror = true;
		}
	}
	
	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_offline(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, dhwtl->id, dhwtl->dhwt->name, PDEV_DHWT, false);
			suberror = true;
		}
	}
	
	// next deal with the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_offline(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, heatsourcel->id, heatsourcel->heats->name, PDEV_HEATS, false);
			suberror = true;
		}
	}
	
	// finally offline the actuators
	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_offline(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, valvel->id, valvel->valve->name, PDEV_VALVE, false);
			suberror = true;
		}
	}
	
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_offline(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			plant_onfline_printerr(ret, pumpl->id, pumpl->pump->name, PDEV_PUMP, false);
			suberror = true;
		}
	}

	memset(&plant->run, 0x0, sizeof(plant->run));
	memset(&plant->pdata, 0x0, sizeof(plant->pdata));
	
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
	const struct s_config * restrict const config = runtime_get()->config;
	const struct s_heating_circuit_l * restrict circuitl;
	const struct s_dhw_tank_l * restrict dhwtl;
	temp_t temp, temp_request = RWCHCD_TEMP_NOREQUEST, temp_req_dhw = RWCHCD_TEMP_NOREQUEST;
	bool dhwt_absolute = false, dhwt_sliding = false, dhwt_reqdhw = false, dhwt_charge = false;

	assert(plant);
	assert(plant->run.online);
	assert(config);

	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		if (!circuitl->circuit->run.online || (ALL_OK != circuitl->status))
			continue;

		temp = circuitl->circuit->run.heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
		if (RWCHCD_TEMP_NOREQUEST != temp)
			plant->run.last_creqtime = now;
	}

	// check if last request exceeds timeout, or if last_creqtime is unset (happens at startup)
	if (!plant->run.last_creqtime || ((now - plant->run.last_creqtime) > config->sleeping_delay))
		plant->pdata.plant_could_sleep = true;
	else
		plant->pdata.plant_could_sleep = false;

	/// XXX @todo should update PCS if any DHWT is active and cannot do electric?

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		if (!dhwtl->dhwt->run.online || (ALL_OK != dhwtl->status))
			continue;

		temp = dhwtl->dhwt->run.heat_request;
		temp_req_dhw = (temp > temp_req_dhw) ? temp : temp_req_dhw;

		// handle DHW charge priority (only in non-electric mode)
		if (dhwtl->dhwt->run.charge_on && !dhwtl->dhwt->run.electric_mode) {
			dhwt_charge = true;
			switch (dhwtl->dhwt->set.dhwt_cprio) {
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
			if (dhwtl->dhwt->set.prio < plant->pdata.dhwt_currprio)
				plant->pdata.dhwt_currprio = dhwtl->dhwt->set.prio;
		}
	}

	// if no heatsource-based DHWT charge is in progress, increase prio threshold (up to max)
	if (!dhwt_charge && (plant->pdata.dhwt_currprio < plant->run.dhwt_maxprio))
		plant->pdata.dhwt_currprio++;

	/*
	 if dhwt_absolute => circuits don't receive heat
	 if dhwt_sliding => circuits can be reduced
	 if dhwt_reqdhw => heat request = max dhw request, else max (max circuit, max dhw)
	 */

	// calculate max of circuit requests and dhwt requests
	temp_request = (temp_req_dhw > temp_request) ? temp_req_dhw : temp_request;

	// select effective heat request
	plant->run.plant_hrequest = dhwt_reqdhw ? temp_req_dhw : temp_request;

	plant->pdata.dhwc_absolute = dhwt_absolute;
	plant->pdata.dhwc_sliding = dhwt_sliding;
}

/**
 * Dispatch heat requests from a plant.
 * @warning currently supports single heat source, all consummers connected to it
 * @param plant target plant
 * @todo XXX logic for multiple heatsources (cascade and/or failover)
 */
static void plant_dispatch_hrequests(struct s_plant * restrict const plant)
{
	struct s_heatsource_l * heatsourcel;
	bool serviced = false;

	assert(plant);
	assert(plant->run.online);

	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		if (!heatsourcel->heats->run.online)
			continue;

		// XXX function call?
		heatsourcel->heats->run.temp_request = plant->run.plant_hrequest;
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
	const struct s_heating_circuit_l * restrict circuitl;
	bool summer = true;

	assert(plant);
	assert(plant->run.online);

	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		if (!circuitl->circuit->run.online)
			continue;
		summer &= circuitl->circuit->bmodel->run.summer;
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
	const struct s_config * restrict const config = runtime_get()->config;
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	int ret;

	assert(plant);
	assert(plant->run.online);
	assert(config);

	// coherent config is ensured during config parsing
	assert(config->summer_run_interval && config->summer_run_duration);

	// don't do anything if summer AND plant asleep aren't in effect
	if (!(plant_summer_ok(plant) && plant->pdata.plant_could_sleep))
		plant->run.summer_timer = now;

	// stop running when duration is exceeded (this also prevents running when summer is first triggered)
	if ((now - plant->run.summer_timer) >= (config->summer_run_interval + config->summer_run_duration)) {
		if (plant->run.summer_timer)	// avoid displaying message at startup
			pr_log(_("Summer maintenance completed"));
		plant->run.summer_timer = now;
	}

	// don't run too often
	if ((now - plant->run.summer_timer) < config->summer_run_interval)
		return (ALL_OK);

	dbgmsg("summer maintenance active");

	// open all valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		if (!valvel->valve->run.online)
			continue;

		if (VA_TYPE_ISOL == valvel->valve->set.type)
			continue;	// don't touch isolation valves

		if (valvel->valve->run.dwht_use)
			continue;	// don't touch DHWT valves when in use

		ret = valve_reqopen_full(valvel->valve);

		if (ALL_OK != ret)
			dbgerr("valve_reqopen_full failed on %d (%d)", valvel->id, ret);
	}

	// set all pumps ON
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		if (!pumpl->pump->run.online)
			continue;

		if (pumpl->pump->run.dwht_use)
			continue;	// don't touch DHWT pumps when in use

		ret = pump_set_state(pumpl->pump, ON, NOFORCE);

		if (ALL_OK != ret)
			dbgerr("pump_set_state failed on %d (%d)", pumpl->id, ret);
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
	const struct s_config * restrict const config = runtime_get()->config;
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	struct s_valve_l * valvel;
	struct s_pump_l * pumpl;
	bool overtemp = false, suberror = false;
	timekeep_t stop_delay = 0;

	assert(config);

	if (!plant)
		return (-EINVALID);
	
	if (!plant->run.online)
		return (-EOFFLINE);

	// run the consummers first so they can set their requested heat input
	// dhwt first
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		dhwtl->status = dhwt_run(dhwtl->dhwt);

		switch (-dhwtl->status) {
			case ALL_OK:
				break;
			default:
				dhwt_offline(dhwtl->dhwt);			// something really bad happened
				// fallthrough
			case EINVALIDMODE:
				dhwtl->dhwt->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
				// fallthrough
			case ESENSORINVAL:
			case ESENSORSHORT:
			case ESENSORDISCON:	// sensor issues are handled by dhwt_run()
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(dhwtl->status, dhwtl->id, dhwtl->dhwt->name, PDEV_DHWT);
				continue;
		}
	}

	// then circuits
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		circuitl->status = hcircuit_run(circuitl->circuit);

		switch (-circuitl->status) {
			case ALL_OK:
				break;
			default:
				hcircuit_offline(circuitl->circuit);		// something really bad happened
				// fallthrough
			case EINVALIDMODE:
				circuitl->circuit->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
				// fallthrough
			case ESENSORINVAL:
			case ESENSORSHORT:
			case ESENSORDISCON:	// sensor issues are handled by hcircuit_run()
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(circuitl->status, circuitl->id, circuitl->circuit->name, PDEV_HCIRC);
				continue;
		}
	}

	// collect and dispatch heat requests
	plant_collect_hrequests(plant);
	plant_dispatch_hrequests(plant);

	// now run the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		heatsourcel->status = heatsource_run(heatsourcel->heats);
		if (ALL_OK == heatsourcel->status) {
			// max stop delay
			stop_delay = (heatsourcel->heats->run.target_consumer_sdelay > stop_delay) ? heatsourcel->heats->run.target_consumer_sdelay : stop_delay;

			// XXX consumer_shift: if a critical shift is in effect it overrides the non-critical one
			assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment for consummer_shift
			plant->pdata.consumer_shift = heatsourcel->heats->run.cshift_crit ? heatsourcel->heats->run.cshift_crit : heatsourcel->heats->run.cshift_noncrit;
		}
		// always update overtemp (which can be triggered with -ESAFETY)
		overtemp = heatsourcel->heats->run.overtemp ? heatsourcel->heats->run.overtemp : overtemp;
		
		switch (-heatsourcel->status) {
			case ALL_OK:
				break;
			default:	// offline the source if anything happens
				heatsource_offline(heatsourcel->heats);	// something really bad happened
				// fallthrough
			case ESENSORINVAL:
			case ESENSORSHORT:
			case ESENSORDISCON:
			case ESAFETY:	// don't do anything, SAFETY procedure handled by logic()/run()
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(heatsourcel->status, heatsourcel->id, heatsourcel->heats->name, PDEV_HEATS);
				continue;	// no further processing for this source
		}
	}

	// reflect global stop delay and overtemp
	plant->pdata.consumer_sdelay = stop_delay;
	plant->pdata.hs_overtemp = overtemp;
	if (overtemp)
		plant->pdata.plant_could_sleep = false;	// disable during overtemp

	if (config->summer_maintenance)
		plant_summer_maintenance(plant);

	// run the valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		valvel->status = valve_run(valvel->valve);

		switch (-valvel->status) {
			case ALL_OK:
			case EDEADBAND:	// not an error
				break;
			default:	// offline the valve if anything happens
				valve_offline(valvel->valve);	// something really bad happened
				// fallthrough
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(valvel->status, valvel->id, valvel->valve->name, PDEV_VALVE);
				continue;	// no further processing for this valve
		}
	}
	
	// run the pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		pumpl->status = pump_run(pumpl->pump);

		switch (-pumpl->status) {
			case ALL_OK:
				break;
			default:	// offline the pump if anything happens
				pump_offline(pumpl->pump);	// something really bad happened
				// fallthrough
			case ENOTCONFIGURED:
			case EOFFLINE:
				suberror = true;
				plant_alarm(pumpl->status, pumpl->id, pumpl->pump->name, PDEV_PUMP);
				continue;	// no further processing for this pump
		}
	}

	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}

/**
 * Trigger anti-legionella charge on all plant DHWTs.
 * For all plant's DHWTs: if the DHWT is online and anti-legionella charge is
 * configured, trigger anti-legionella charge.
 * @param plant the target plant
 */
void plant_dhwt_legionella_trigger(struct s_plant * restrict const plant)
{
	struct s_dhw_tank_l * dhwtl;

	assert(plant);
	assert(plant->run.online);

	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		if (!dhwtl->dhwt->run.online)
			continue;

		if (dhwtl->dhwt->set.anti_legionella)
			dhwtl->dhwt->run.legionella_on = true;
	}
}
