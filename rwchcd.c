//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

//* TODO
// Setup
// Computation
// Control
// accounting (separate module that periodically polls states and write them to timestamped registry)
// Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
// UI + programming
// handle summer switchover
// connection of multiple instances
// multiple heatsources + switchover (e.g. wood furnace -> gas/fuel boiler)

// http://www.energieplus-lesite.be/index.php?id=10963


#include <unistd.h>	// sleep/usleep
#include <stdlib.h>	// exit
#include <pthread.h>
#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_lcd.h"
#include "rwchcd_spi.h"

static void * thread_hardware(void *arg)
{
	
}

static inline uint8_t rid_to_rwchcaddr(unsigned int id)
{
	if (id < 8)
		return (id-1);
	else
		return (id);
}


static int init_process()
{
	struct s_runtime * restrict const runtime = get_runtime();
	struct s_config * restrict config = NULL;
	struct s_plant * restrict plant = NULL;
	struct s_heatsource * restrict heatsource = NULL;
	struct s_heating_circuit * restrict circuit = NULL;
	struct s_dhw_tank * restrict dhwt = NULL;
	struct s_boiler_priv * restrict boiler = NULL;
	int ret;

	/* init hardware */
	
	ret = hardware_init();
	if (ret) {
		dbgerr("hardware init error: %d", ret);
		return (ret);
	}

	/* init runtime */
	runtime_init();

	/* init config */

	config = config_new();
	ret = config_init(config);
	if (ret) {
		dbgerr("config init error: %d", ret);
		return (ret);
	}
	config_set_building_tau(config, 10 * 60 * 60);	// XXX 10 hours
	config_set_nsensors(config, 4);	// XXX 4 sensors
	config_set_outdoor_sensorid(config, 1);
	config_set_tfrostmin(config, celsius_to_temp(5));	// XXX frost protect at 5C
	config_set_tsummer(config, celsius_to_temp(18));	// XXX summer switch at 18C

	// XXX add firmware config bits here

	config->configured = true;
//	config_save(config);

	// attach config to runtime
	runtime->config = config;

	/* init plant */

	// create a new plant
	plant = plant_new();
	if (!plant) {
		dbgerr("plant creation failed");
		return (-EOOM);
	}

	// create a new heat source for the plant
	heatsource = plant_new_heatsource(plant, BOILER);
	if (!heatsource) {
		dbgerr("heatsource creation failed");
		return (-EOOM);
	}

	// configure that source	XXX REVISIT
	boiler = heatsource->priv;
	boiler->histeresis = deltaK_to_temp(8);
	boiler->limit_tmax = celsius_to_temp(90);
	boiler->limit_tmin = celsius_to_temp(45);
	boiler->id_temp = 2;	// XXX VALIDATION
	config->rWCHC_settings.addresses.S_burner = 2-1;			// XXX INTERNAL CONFIG
	boiler->id_temp_outgoing = boiler->id_temp;
	boiler->burner_1 = hardware_relay_new();
	if (!boiler->burner_1) {
		dbgerr("burner relay creation failed");
		return (-EOOM);
	}
	hardware_relay_set_id(boiler->burner_1, 14);	// XXX 2nd relay
	config->rWCHC_settings.addresses.T_burner = rid_to_rwchcaddr(14);	// XXX INTERNAL CONFIG
	boiler->burner_1->configured = true;
	boiler->set_burner_min_time = 2 * 60;	// XXX 2 minutes
	boiler->set_sleeping_time = 2 * 24 * 60 * 60;	// XXX 2 days
	heatsource->set_runmode = RM_AUTO;	// use global setting
	heatsource->configured = true;

	// create a new circuit for the plant
	circuit = plant_new_circuit(plant);
	if (!circuit) {
		dbgerr("circuit creation failed");
		return (-EOOM);
	}

	// configure that circuit
	circuit->limit_wtmax = celsius_to_temp(85);
	circuit->limit_wtmin = celsius_to_temp(20);
	circuit->set_tcomfort = celsius_to_temp(20.5F);
	circuit->set_teco = celsius_to_temp(16);
	circuit->set_tfrostfree = celsius_to_temp(7);
	circuit->set_outhoff_comfort = circuit->set_tcomfort - deltaK_to_temp(4);
	circuit->set_outhoff_eco = circuit->set_teco - deltaK_to_temp(4);
	circuit->set_outhoff_frostfree = circuit->set_tfrostfree - deltaK_to_temp(4);
	circuit->set_outhoff_histeresis = deltaK_to_temp(1);
	circuit->id_temp_outgoing = 3;	// XXX VALIDATION
	config->rWCHC_settings.addresses.S_water = 3-1;				// XXX INTERNAL CONFIG
	circuit->id_temp_return = 4;	// XXX VALIDATION
	circuit->set_temp_inoffset = deltaK_to_temp(10);
	circuit->tlaw_data.tout1 = celsius_to_temp(-5);
	circuit->tlaw_data.twater1 = celsius_to_temp(70);
	circuit->tlaw_data.tout2 = celsius_to_temp(15);
	circuit->tlaw_data.twater2 = celsius_to_temp(35);
	circuit_make_linear(circuit);

	// create a valve for that circuit
	circuit->valve = plant_new_valve(plant);
	if (!circuit->valve) {
		dbgerr("valve creation failed");
		return (-EOOM);
	}

	// configure that valve
	circuit->valve->set_tdeadzone = deltaK_to_temp(2);
	circuit->valve->set_deadband = 4;	// XXX 4% minimum increments
	circuit->valve->set_ete_time = 120;	// XXX 120 s
	circuit->valve->id_temp1 = boiler->id_temp_outgoing;
	circuit->valve->id_temp2 = circuit->id_temp_return;
	circuit->valve->id_tempout = circuit->id_temp_outgoing;

	// create and configure two relays for that valve
	circuit->valve->open = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->open, 11);
	config->rWCHC_settings.addresses.T_Vopen = rid_to_rwchcaddr(11);		// XXX INTERNAL CONFIG
	circuit->valve->open->configured = true;

	circuit->valve->close = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->close, 10);
	config->rWCHC_settings.addresses.T_Vclose = rid_to_rwchcaddr(10);	// XXX INTERNAL CONFIG
	circuit->valve->close->configured = true;

	circuit->valve->configured = true;

	// create a pump for that circuit
	circuit->pump = plant_new_pump(plant);
	if (!circuit->pump) {
		dbgerr("pump creation failed");
		return (-EOOM);
	}

	// configure that pump
	circuit->pump->set_cooldown_time = 10 * 60;	// XXX 10 minutes

	// create and configure a relay for that pump
	circuit->pump->relay = hardware_relay_new();
	hardware_relay_set_id(circuit->pump->relay, 9);
	config->rWCHC_settings.addresses.T_pump = rid_to_rwchcaddr(9);		// XXX INTERNAL CONFIG
	circuit->pump->relay->configured = true;

	circuit->pump->configured = true;

	circuit->set_runmode = RM_AUTO;		// use global setting

	circuit->configured = true;

	// create a new DHWT for the plant
	dhwt = plant_new_dhwt(plant);
	if (!dhwt) {
		dbgerr("dhwt creation failed");
		return (-EOOM);
	}

	// configure that dhwt
	dhwt->id_temp_bottom = boiler->id_temp;
	dhwt->limit_tmin = celsius_to_temp(5);
	dhwt->limit_tmax = celsius_to_temp(60);
	dhwt->limit_wintmax = celsius_to_temp(90);
	dhwt->set_tcomfort = celsius_to_temp(50);
	dhwt->set_teco = celsius_to_temp(40);
	dhwt->set_tfrostfree = celsius_to_temp(10);	// XXX REVISIT RELATIONS BETWEEN TEMPS
	dhwt->histeresis = deltaK_to_temp(10);
	dhwt->set_temp_inoffset = deltaK_to_temp(0);	// Integrated tank
	dhwt->set_runmode = RM_AUTO;	// use global setting
	dhwt->configured = true;

	plant->configured = true;

	config_save(config);				// XXX HERE BECAUSE OF INTERNAL CONFIG HACKS

	// assign plant to runtime
	runtime->plant = plant;

	// bring the hardware online
	hardware_online();
	
	// finally bring the runtime online (resets actuators)
	return (runtime_online());
}

/*
 temp conversion from sensor raw value + calibration
 temp boiler: target water temp + hist; ceil and floor
 temp water: water curve with outdoor temp + timing (PID?) comp (w/ building constant) + indoor comp
 valve position: PID w/ total run time from C to O
 */

int main(void)
{
	int ret;

	ret = init_process();
	if (ret != ALL_OK) {
		dbgerr("init_proccess failed (%d)", ret);
		if (ret == -ESPI)	// XXX HACK
			exit(ret);
	}

	while (1) {
		hardware_run();

		// test read peripherals
		ret = hardware_rwchcperiphs_read();
		if (ret)
			dbgerr("hardware_rwchcperiphs_read failed (%d)", ret);

		ret = runtime_run();
		if (ret)
			dbgerr("runtime_run returned: %d", ret);

		ret = hardware_rwchcperiphs_write();
		if (ret)
			dbgerr("hardware_rwchcperiphs_write failed (%d)", ret);

		sleep(1);
	}
}
