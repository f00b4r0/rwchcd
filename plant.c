//
//  plant.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation implementation.
 * @todo plant_save()/plant_restore() (for e.g. dynamically created plants)
 * @todo multiple heatsources: in switchover mode (e.g. wood furnace + fuel:
 * switch to fuel when wood dies out) and cascade mode (for large systems).
 * In this context, a "plant" should logically be a collection of consummers
 * and heatsources all connected to each other: in a plant, all the heatsources
 * are providing heat to all of the plant's consummers.
 */

#include <stdlib.h>	// calloc/free
#include <unistd.h>	// sleep
#include <math.h>	// roundf
#include <assert.h>
#include <string.h>

#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "logic.h"
#include "plant.h"
#include "pump.h"
#include "valve.h"
#include "hcircuit.h"
#include "dhwt.h"
#include "heatsource.h"
#include "models.h"	// s_bmodel for plant_summer_ok()
#include "storage.h"

#if 0
static const storage_version_t Plant_sversion = 2;

/**
 * Save plant.
 * @param plant target plant
 * @return exec status
 */
static int plant_save(const struct s_plant * restrict const plant)
{
	assert(plant);

	if (!plant->configured)
		return (-ENOTCONFIGURED);

	return (storage_dump("plant", &Plant_sversion, &plant->run, sizeof(plant->run)));
}

/**
 * Restore plant from permanent storage.
 * @param plant plant whose run structure will be restored if possible,
 * left untouched otherwise
 * @return exec status
 */
static int plant_restore(struct s_plant * restrict const plant)
{
	struct s_plant temp_plant;
	storage_version_t sversion;
	int ret;

	// try to restore key elements of last runtime
	ret = storage_fetch("plant", &sversion, &temp_plant.run, sizeof(temp_plant.run));
	if (ALL_OK == ret) {
		if (Plant_sversion != sversion)
			return (-EMISMATCH);

		memcpy(&plant->run, &temp_plant.run, sizeof(plant->run));
		pr_log(_("Plant state restored"));
	}

	return (ALL_OK);
}
#endif

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

/**
 * Bring plant online.
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
			dbgerr("pump_online failed, id: %d (%d)", pumpl->id, ret);
			pump_offline(pumpl->pump);
			suberror = true;
		}
	}

	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_online(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("valve_online failed, id: %d (%d)", valvel->id, ret);
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
			dbgerr("circuit_online failed, id: %d (%d)", circuitl->id, ret);
			hcircuit_offline(circuitl->circuit);
			suberror = true;
		}
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("dhwt_online failed, id: %d (%d)", dhwtl->id, ret);
			dhwt_offline(dhwtl->dhwt);
			suberror = true;
		}
	}

	// finally online the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_online(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("heatsource_online failed, id: %d (%d)", heatsourcel->id, ret);
			heatsource_offline(heatsourcel->heats);
			suberror = true;
		}
	}

	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
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
			dbgerr("circuit_offline failed, id: %d (%d)", circuitl->id, ret);
			suberror = true;
		}
	}
	
	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_offline(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("dhwt_offline failed, id: %d (%d)", dhwtl->id, ret);
			suberror = true;
		}
	}
	
	// next deal with the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_offline(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("heatsource_offline failed, id: %d (%d)", heatsourcel->id, ret);
			suberror = true;
		}
	}
	
	// finally offline the actuators
	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_offline(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("valve_offline failed, id: %d (%d)", valvel->id, ret);
			suberror = true;
		}
	}
	
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_offline(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("pump_offline failed, id: %d (%d)", pumpl->id, ret);
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
 * Collect heat requests from a plant.
 * @param plant target plant
 */
static void plant_collect_hrequests(struct s_plant * restrict const plant)
{
	const timekeep_t now = timekeep_now();
	struct s_runtime * restrict const runtime = runtime_get();
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	temp_t temp, temp_request = RWCHCD_TEMP_NOREQUEST, temp_req_dhw = RWCHCD_TEMP_NOREQUEST;
	bool dhwt_absolute = false, dhwt_sliding = false, dhwt_reqdhw = false;

	assert(plant);
	assert(runtime);

	// for consummers in runtime scheme, collect heat requests and max them
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		if (!circuitl->circuit->run.online)
			continue;

		temp = circuitl->circuit->run.heat_request;
		temp_request = (temp > temp_request) ? temp : temp_request;
		if (RWCHCD_TEMP_NOREQUEST != temp)
			plant->run.last_creqtime = now;
	}

	// check if last request exceeds timeout
	if ((now - plant->run.last_creqtime) > runtime->config->sleeping_delay)
		plant->pdata.plant_could_sleep = true;
	else
		plant->pdata.plant_could_sleep = false;

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		if (!dhwtl->dhwt->run.online)
			continue;

		temp = dhwtl->dhwt->run.heat_request;
		temp_req_dhw = (temp > temp_req_dhw) ? temp : temp_req_dhw;

		// handle DHW charge priority
		if (dhwtl->dhwt->run.charge_on) {
			switch (dhwtl->dhwt->set.dhwt_cprio) {
				case DHWTP_SLIDDHW:
					dhwt_reqdhw = true;
				case DHWTP_SLIDMAX:
					dhwt_sliding = true;
					break;
				case DHWTP_ABSOLUTE:
					dhwt_absolute = true;
				case DHWTP_PARALDHW:
					dhwt_reqdhw = true;
				case DHWTP_PARALMAX:
				default:
					/* nothing */
					break;
			}
		}
	}

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
 * @todo XXX logic for multiple heatsources (cascade and/or failover)
 * @param plant target plant
 */
static void plant_dispatch_hrequests(struct s_plant * restrict const plant)
{
	struct s_heatsource_l * heatsourcel;
	bool serviced = false;

	assert(plant);

	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		if (!heatsourcel->heats->run.online)
			continue;

		// XXX function call?
		heatsourcel->heats->run.temp_request = plant->run.plant_hrequest;
		serviced = true;
	}

	if (!serviced)
		dbgerr("No heatsource available!");
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
	struct s_heating_circuit_l * circuitl;
	bool summer = true;

	assert(plant);

	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		if (!circuitl->circuit->run.online)
			continue;
		summer &= circuitl->circuit->bmodel->run.summer;
	}

	return (summer);
}

/**
 * Plant summer maintenance operations.
 * When summer conditions are met, the pumps and valves are periodically actuated.
 * The idea of this function is to run as an override filter in the plant_run()
 * loop so that during summer maintenance, the state of these actuators is
 * overriden.
 * @param plant target plant
 * @return exec status
 * @todo sequential run (instead of parallel)
 */
static int plant_summer_maintenance(struct s_plant * restrict const plant)
{
#define SUMMER_RUN_INTVL	(60*60*24*7*TIMEKEEP_SMULT)	///< 1 week
#define SUMMER_RUN_DURATION	(60*5*TIMEKEEP_SMULT)		///< 5 minutes
	const timekeep_t now = timekeep_now();
	const struct s_runtime * restrict const runtime = runtime_get();
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	int ret;

	assert(plant);
	assert(runtime);

	// don't do anything if summer AND plant asleep aren't in effect
	if (!(plant_summer_ok(plant) && plant->pdata.plant_could_sleep))
		plant->run.summer_timer = now;

	// stop running when duration is exceeded (this also prevents running when summer is first triggered)
	if ((now - plant->run.summer_timer) >= (SUMMER_RUN_INTVL + SUMMER_RUN_DURATION)) {
		if (plant->run.summer_timer)	// avoid displaying message at startup
			pr_log(_("Summer maintenance completed"));
		plant->run.summer_timer = now;
	}

	// don't run too often
	if ((now - plant->run.summer_timer) < SUMMER_RUN_INTVL)
		return (ALL_OK);

	dbgmsg("summer maintenance active");

	// open all valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		if (!valvel->valve->run.online)
			continue;

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
 * @todo separate error handler
 */
int plant_run(struct s_plant * restrict const plant)
{
	struct s_runtime * restrict const runtime = runtime_get();
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	struct s_valve_l * valvel;
	struct s_pump_l * pumpl;
	int ret;
	bool suberror = false;
	timekeep_t stop_delay = 0;

	assert(runtime);

	if (!plant)
		return (-EINVALID);
	
	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// run the consummers first so they can set their requested heat input
	// dhwt first
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = logic_dhwt(dhwtl->dhwt);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = dhwt_run(dhwtl->dhwt);

		dhwtl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:
				dhwt_offline(dhwtl->dhwt);			// something really bad happened
			case -EINVALIDMODE:
				dhwtl->dhwt->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:	// sensor issues are handled by dhwt_run()
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("logic_dhwt/run failed on %d (%d)", dhwtl->id, ret);
				continue;
		}
	}

	// then circuits
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = logic_hcircuit(circuitl->circuit);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = hcircuit_run(circuitl->circuit);

		circuitl->status = ret;

		switch (ret) {
			case ALL_OK:
				break;
			default:
				hcircuit_offline(circuitl->circuit);		// something really bad happened
			case -EINVALIDMODE:
				circuitl->circuit->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:	// sensor issues are handled by hcircuit_run()
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("logic_hcircuit/run failed on %d (%d)", circuitl->id, ret);
				continue;
		}
	}

	// collect and dispatch heat requests
	plant_collect_hrequests(plant);
	plant_dispatch_hrequests(plant);

	// now run the heat sources
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = logic_heatsource(heatsourcel->heats);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = heatsource_run(heatsourcel->heats);
		if (ALL_OK == ret) {
			// max stop delay
			stop_delay = (heatsourcel->heats->run.target_consumer_sdelay > stop_delay) ? heatsourcel->heats->run.target_consumer_sdelay : stop_delay;

			// XXX consumer_shift: if a critical shift is in effect it overrides the non-critical one
			plant->pdata.consumer_shift = heatsourcel->heats->run.cshift_crit ? heatsourcel->heats->run.cshift_crit : heatsourcel->heats->run.cshift_noncrit;
		}

		heatsourcel->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:	// offline the source if anything happens
				heatsource_offline(heatsourcel->heats);	// something really bad happened
			case -ENOTCONFIGURED:
			case -EOFFLINE:
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:
			case -ESAFETY:	// don't do anything, SAFETY procedure handled by logic()/run()
				suberror = true;
				dbgerr("logic_heatsource/run failed on %d (%d)", heatsourcel->id, ret);
				continue;	// no further processing for this source
		}
	}

	// reflect global stop delay
	plant->pdata.consumer_sdelay = stop_delay;

	if (runtime->config->summer_maintenance)
		plant_summer_maintenance(plant);

	// run the valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_logic(valvel->valve);
		if (ALL_OK == ret)
			ret = valve_run(valvel->valve);

		valvel->status = ret;
		
		switch (ret) {
			case ALL_OK:
			case -EDEADBAND:	// not an error
				break;
			default:	// offline the valve if anything happens
				valve_offline(valvel->valve);	// something really bad happened
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("valve_run failed on %d (%d)", valvel->id, ret);
				continue;	// no further processing for this valve
		}
	}
	
	// run the pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_run(pumpl->pump);
		
		pumpl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:	// offline the pump if anything happens
				pump_offline(pumpl->pump);	// something really bad happened
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("pump_run failed on %d (%d)", pumpl->id, ret);
				continue;	// no further processing for this valve
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

	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		if (!dhwtl->dhwt->run.online)
			continue;

		if (dhwtl->dhwt->set.anti_legionella)
			dhwtl->dhwt->run.legionella_on = true;
	}
}
