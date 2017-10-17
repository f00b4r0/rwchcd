//
//  plant.c
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
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
#include "hardware.h"
#include "logic.h"
#include "plant.h"
#include "models.h"
#include "alarms.h"	// alarms_raise()
#include "pump.h"
#include "valve.h"
#include "circuit.h"
#include "dhwt.h"
#include "heatsource.h"

/**
 * Create a new pump and attach it to the plant.
 * @param plant the plant to attach the pump to
 * @param name @b UNIQUE pump name (or NULL). A local copy is created if set
 * @return pointer to the created pump
 */
struct s_pump * plant_new_pump(struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_pump_l * restrict pumpl;
	struct s_pump * restrict pump = NULL;
	struct s_pump_l * restrict pumpelmt = NULL;
	char * restrict str = NULL;

	if (!plant)
		goto fail;

	// deal with name
	if (name) {
		// ensure unique name
		for (pumpl = plant->pump_head; pumpl; pumpl = pumpl->next) {
			if (!strcmp(pumpl->pump->name, name))
				goto fail;
		}

		str = strdup(name);
		if (!str)
			goto fail;
	}

	// create a new pump. calloc() sets good defaults
	pump = calloc(1, sizeof(*pump));
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
 * Create a new valve and attach it to the plant.
 * @param plant the plant to attach the valve to
 * @param name @b UNIQUE valve name (or NULL). A local copy is created if set
 * @return pointer to the created valve
 */
struct s_valve * plant_new_valve(struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_valve_l * restrict valvel;
	struct s_valve * restrict valve = NULL;
	struct s_valve_l * restrict valveelmt = NULL;
	char * restrict str = NULL;

	if (!plant)
		goto fail;

	// deal with name
	if (name) {
		// ensure unique name
		for (valvel = plant->valve_head; valvel; valvel = valvel->next) {
			if (!strcmp(valvel->valve->name, name))
				goto fail;
		}

		str = strdup(name);
		if (!str)
			goto fail;
	}

	// create a new valve. calloc() sets good defaults
	valve = calloc(1, sizeof(*valve));
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
 * Create a new heating circuit and attach it to the plant.
 * @param plant the plant to attach the circuit to
 * @param name @b UNIQUE circuit name (or NULL). A local copy is created if set
 * @return pointer to the created heating circuit
 */
struct s_heating_circuit * plant_new_circuit(struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_heating_circuit_l * restrict circuitl;
	struct s_heating_circuit * restrict circuit = NULL;
	struct s_heating_circuit_l * restrict circuitelement = NULL;
	char * restrict str = NULL;

	if (!plant)
		goto fail;

	// deal with name
	if (name) {
		// ensure unique name
		for (circuitl = plant->circuit_head; circuitl; circuitl = circuitl->next) {
			if (!strcmp(circuitl->circuit->name, name))
				goto fail;
		}

		str = strdup(name);
		if (!str)
			goto fail;
	}

	// create a new circuit. calloc() sets good defaults
	circuit = calloc(1, sizeof(*circuit));
	if (!circuit)
		goto fail;

	// set name
	circuit->name = str;

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
 * Create a new dhw tank and attach it to the plant.
 * @param plant the plant to attach the tank to
 * @param name @b UNIQUE dhwt name (or NULL). A local copy is created if set
 * @return pointer to the created tank
 */
struct s_dhw_tank * plant_new_dhwt(struct s_plant * restrict const plant, const char * restrict const name)
{
	const struct s_dhw_tank_l * restrict dhwtl;
	struct s_dhw_tank * restrict dhwt = NULL;
	struct s_dhw_tank_l * restrict dhwtelement = NULL;
	char * restrict str = NULL;

	if (!plant)
		goto fail;

	// deal with name
	if (name) {
		// ensure unique name
		for (dhwtl = plant->dhwt_head; dhwtl; dhwtl = dhwtl->next) {
			if (!strcmp(dhwtl->dhwt->name, name))
				goto fail;
		}

		str = strdup(name);
		if (!str)
			goto fail;
	}

	// create a new tank. calloc() sets good defaults
	dhwt = calloc(1, sizeof(*dhwt));
	if (!dhwt)
		goto fail;

	// set name
	dhwt->name = str;

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
 * Create a new heatsource in the plant
 * @param plant the target plant
 * @param name @b UNIQUE heatsource name (or NULL). A local copy is created if set
 * @param type the heatsource type to create
 * @return pointer to the created source
 */
struct s_heatsource * plant_new_heatsource(struct s_plant * restrict const plant, const char * restrict const name,
					   const enum e_heatsource_type type)
{
	const struct s_heatsource_l * restrict sourcel;
	struct s_heatsource * restrict source = NULL;
	struct s_heatsource_l * restrict sourceelement = NULL;
	char * restrict str = NULL;

	if (!plant)
		goto fail;

	// deal with name
	if (name) {
		// ensure unique name
		for (sourcel = plant->heats_head; sourcel; sourcel = sourcel->next) {
			if (!strcmp(sourcel->heats->name, name))
				goto fail;
		}

		str = strdup(name);
		if (!str)
			goto fail;
	}

	// create a new source. calloc() sets good defaults
	source = calloc(1, sizeof(*source));
	if (!source)
		goto fail;

	switch (type) {
		case BOILER:
			heatsource_make_boiler(source);
			break;
		case NONE:
		default:
			break;
	}

	// check we have a priv element except for type NONE
	if (!source->priv && (NONE != type))
		goto fail;

	source->set.type = type;

	// set name
	source->name = str;

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
	if (source && source->hs_del_priv)
		source->hs_del_priv(source->priv);
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
	struct s_plant * const plant = calloc(1, sizeof(struct s_plant));

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
		circuit_del(circuitelement->circuit);
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

	// online the actuators first
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_online(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("pump_online failed, id: %d (%d)", pumpl->id, ret);
			pump_offline(pumpl->pump);
			pumpl->pump->run.online = false;
			suberror = true;
		}
		else
			pumpl->pump->run.online = true;
	}

	// valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		ret = valve_online(valvel->valve);
		valvel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("valve_online failed, id: %d (%d)", valvel->id, ret);
			valve_offline(valvel->valve);
			valvel->valve->run.online = false;
			suberror = true;
		}
		else
			valvel->valve->run.online = true;
	}
	
	// next deal with the consummers
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_online(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("circuit_online failed, id: %d (%d)", circuitl->id, ret);
			circuit_offline(circuitl->circuit);
			circuitl->circuit->run.online = false;
			suberror = true;
		}
		else
			circuitl->circuit->run.online = true;
	}

	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_online(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("dhwt_online failed, id: %d (%d)", dhwtl->id, ret);
			dhwt_offline(dhwtl->dhwt);
			dhwtl->dhwt->run.online = false;
			suberror = true;
		}
		else
			dhwtl->dhwt->run.online = true;
	}

	// finally online the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_online(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("heatsource_online failed, id: %d (%d)", heatsourcel->id, ret);
			heatsource_offline(heatsourcel->heats);
			heatsourcel->heats->run.online = false;
			suberror = true;
		}
		else
			heatsourcel->heats->run.online = true;
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
	
	// offline the consummers first
	// circuits first
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = circuit_offline(circuitl->circuit);
		circuitl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("circuit_offline failed, id: %d (%d)", circuitl->id, ret);
			suberror = true;
		}
		circuitl->circuit->run.online = false;
	}
	
	// then dhwt
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = dhwt_offline(dhwtl->dhwt);
		dhwtl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("dhwt_offline failed, id: %d (%d)", dhwtl->id, ret);
			suberror = true;
		}
		dhwtl->dhwt->run.online = false;
	}
	
	// next deal with the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = heatsource_offline(heatsourcel->heats);
		heatsourcel->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("heatsource_offline failed, id: %d (%d)", heatsourcel->id, ret);
			suberror = true;
		}
		heatsourcel->heats->run.online = false;
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
		valvel->valve->run.online = false;
	}
	
	// pumps
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
		ret = pump_offline(pumpl->pump);
		pumpl->status = ret;
		
		if (ALL_OK != ret) {
			dbgerr("pump_offline failed, id: %d (%d)", pumpl->id, ret);
			suberror = true;
		}
		pumpl->pump->run.online = false;
	}
	
	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
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
static int plant_summer_maintenance(const struct s_plant * restrict const plant)
{
#define SUMMER_RUN_INTVL	60*60*24*7	///< 1 week
#define SUMMER_RUN_DURATION	60*5		///< 5 minutes
	static time_t timer_start = 0;
	const time_t now = time(NULL);
	const struct s_runtime * restrict const runtime = get_runtime();
	struct s_pump_l * pumpl;
	struct s_valve_l * valvel;
	int ret;

	// don't do anything if summer AND plant asleep aren't in effect
	if (!(runtime->summer && runtime->plant_could_sleep))
		timer_start = now;

	// stop running when duration is exceeded (this also prevents running when summer is first triggered)
	if ((now - timer_start) >= (SUMMER_RUN_INTVL + SUMMER_RUN_DURATION)) {
		timer_start = now;
		pr_log("summer maintenance completed");
	}

	// don't run too often
	if ((now - timer_start) < SUMMER_RUN_INTVL)
		return (ALL_OK);

	dbgmsg("summer maintenance active");

	// open all valves
	for (valvel = plant->valve_head; valvel != NULL; valvel = valvel->next) {
		if (valvel->valve->run.dwht_use)
			continue;	// don't touch DHWT valves when in use

		ret = valve_reqopen_full(valvel->valve);

		if (ALL_OK != ret)
			dbgerr("valve_reqopen_full failed on %d (%d)", valvel->id, ret);
	}

	// set all pumps ON
	for (pumpl = plant->pump_head; pumpl != NULL; pumpl = pumpl->next) {
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
 * @todo XXX TODO: currently supports single heat source, all consummers connected to it
 */
int plant_run(struct s_plant * restrict const plant)
{
	struct s_runtime * restrict const runtime = get_runtime();
	struct s_heating_circuit_l * circuitl;
	struct s_dhw_tank_l * dhwtl;
	struct s_heatsource_l * heatsourcel;
	struct s_valve_l * valvel;
	struct s_pump_l * pumpl;
	int ret;
	bool sleeping = true, suberror = false, dhwc_absolute = false;
	time_t stop_delay = 0;

	assert(plant);
	
	if (!plant->configured)
		return (-ENOTCONFIGURED);

	// run the consummers first so they can set their requested heat input
	// dhwt first (to handle absolute priority)
	for (dhwtl = plant->dhwt_head; dhwtl != NULL; dhwtl = dhwtl->next) {
		ret = logic_dhwt(dhwtl->dhwt);
		if (ALL_OK == ret) {	// run() only if logic() succeeds
			ret = dhwt_run(dhwtl->dhwt);
			if (ALL_OK == ret) {
				if (dhwt_in_absolute_charge(dhwtl->dhwt))
					dhwc_absolute = true;
			}
		}

		dhwtl->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:
				dhwt_offline(dhwtl->dhwt);
			case -EINVALIDMODE:
				dhwtl->dhwt->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("logic_dhwt/run failed on %d (%d)", dhwtl->id, ret);
				continue;
		}
	}

	// update dhwc_absolute
	plant->dhwc_absolute = dhwc_absolute;

	// then circuits
	for (circuitl = plant->circuit_head; circuitl != NULL; circuitl = circuitl->next) {
		ret = logic_circuit(circuitl->circuit);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = circuit_run(circuitl->circuit);

		circuitl->status = ret;

		switch (ret) {
			case ALL_OK:
				break;
			default:
				circuit_offline(circuitl->circuit);
			case -EINVALIDMODE:
				circuitl->circuit->set.runmode = RM_FROSTFREE;	// XXX force mode to frost protection (this should be part of an error handler)
			case -ESENSORINVAL:
			case -ESENSORSHORT:
			case -ESENSORDISCON:	// sensor issues are handled by circuit_run()
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("logic_circuit/run failed on %d (%d)", circuitl->id, ret);
				continue;
		}
	}

	// finally run the heat source
	assert(plant->heats_n <= 1);	// XXX TODO: only one source supported at the moment
	for (heatsourcel = plant->heats_head; heatsourcel != NULL; heatsourcel = heatsourcel->next) {
		ret = logic_heatsource(heatsourcel->heats);
		if (ALL_OK == ret)	// run() only if logic() succeeds
			ret = heatsource_run(heatsourcel->heats);

		heatsourcel->status = ret;
		
		switch (ret) {
			case ALL_OK:
				break;
			default:	// offline the source if anything happens
				heatsource_offline(heatsourcel->heats);
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
		
		if (!heatsourcel->heats->run.could_sleep)	// if (a) heatsource isn't sleeping then the plant isn't sleeping
			sleeping = heatsourcel->heats->run.could_sleep;
		
		// max stop delay
		stop_delay = (heatsourcel->heats->run.target_consumer_sdelay > stop_delay) ? heatsourcel->heats->run.target_consumer_sdelay : stop_delay;

		// XXX consumer_shift: if a critical shift is in effect it overrides the non-critical one
		plant->consumer_shift = heatsourcel->heats->run.cshift_crit ? heatsourcel->heats->run.cshift_crit : heatsourcel->heats->run.cshift_noncrit;
	}

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
				valve_offline(valvel->valve);
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
				pump_offline(pumpl->pump);
			case -ENOTCONFIGURED:
			case -EOFFLINE:
				suberror = true;
				dbgerr("pump_run failed on %d (%d)", pumpl->id, ret);
				continue;	// no further processing for this valve
		}
	}
	
	// reflect global sleeping state
	runtime->plant_could_sleep = sleeping;

	// reflect global stop delay
	plant->consumer_sdelay = stop_delay;
	
	if (suberror)
		return (-EGENERIC);	// further processing required to figure where the error(s) is/are.
	else
		return (ALL_OK);
}
