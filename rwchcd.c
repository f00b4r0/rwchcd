//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 @mainpage
 rwchcd: a central heating controller daemon for rWCHC.
 
 @author Thibaut VARENE
 @date 2016
 @copyright GPLv2
 
 Copyright: (C) 2016 Thibaut VARENE
 
 Home page: http://hacks.slashdirt.org/hw/rwchc/
 */

/**
 * @file
 * Main program.
 * @todo:
 * Setup
 * Computation
 * Control
 * accounting (separate module that periodically polls states and write them to timestamped registry)
 * Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
 * UI + programming
 * connection of multiple instances
 * multiple heatsources + switchover (e.g. wood furnace -> gas/fuel boiler)
 */

// http://www.energieplus-lesite.be/index.php?id=10963

#include <unistd.h>	// sleep/usleep/setuid
#include <stdlib.h>	// exit
#include <signal.h>
#include <pthread.h>
#include <err.h>

#include "rwchcd.h"
#include "rwchcd_lib.h"
#include "rwchcd_hardware.h"
#include "rwchcd_plant.h"
#include "rwchcd_config.h"
#include "rwchcd_runtime.h"
#include "rwchcd_dbus.h"

#include "svn_version.h"

#define RWCHCD_PRIO	20	///< Desired run priority
#define RWCHCD_UID	65534	///< Desired run uid
#define RWCHCD_GID	65534	///< Desired run gid

static int master_thread_sem = 0;

static const char Version[] = SVN_REV;	///< SVN_REV is defined in makefile

/**
 * Daemon signal handler.
 * Handles SIGINT and SIGTERM for graceful shutdown.
 * @param signum signal to handle.
 */
static void sig_handler(int signum)
{
	switch (signum) {
		case SIGINT:
		case SIGTERM:
			dbus_quit();
			break;
		default:
			break;
	}
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
	
	// XXX firmware config bits here
	hardware_config_limit_set(HLIM_FROSTMIN, 3);
	hardware_config_limit_set(HLIM_BOILERMIN, 50);
	hardware_config_limit_set(HLIM_BOILERMAX, 70);
	hardware_config_addr_set(HADDR_SBURNER, 2);
	hardware_config_addr_set(HADDR_TBURNER, 14);
	hardware_config_addr_set(HADDR_SWATER, 3);
	hardware_config_addr_set(HADDR_TVOPEN, 11);
	hardware_config_addr_set(HADDR_TVCLOSE, 10);
	hardware_config_addr_set(HADDR_TPUMP, 9);
	hardware_config_store();

	/* init runtime */
	runtime_init();

	/* init config */

	config = config_new();
	ret = config_init(config);
	if (ret) {
		dbgerr("config init error: %d", ret);
		return (ret);
	}
	
	if (!config->restored) {
		config_set_building_tau(config, 10 * 60 * 60);	// XXX 10 hours
		config_set_nsensors(config, 4);	// XXX 4 sensors
		config_set_outdoor_sensorid(config, 1);
		config_set_tsummer(config, celsius_to_temp(18));	// XXX summer switch at 18C

		// circuit defaults
		config->def_circuit.t_comfort = celsius_to_temp(20.0F);
		config->def_circuit.t_eco = celsius_to_temp(16);
		config->def_circuit.t_frostfree = celsius_to_temp(7);
		config->def_circuit.outhoff_comfort = config->def_circuit.t_comfort - deltaK_to_temp(2);	// XXX should be deltas and not temps ?
		config->def_circuit.outhoff_eco = config->def_circuit.t_eco - deltaK_to_temp(2);
		config->def_circuit.outhoff_frostfree = config->def_circuit.t_frostfree - deltaK_to_temp(4);
		config->def_circuit.outhoff_histeresis = deltaK_to_temp(1);
		config->def_circuit.limit_wtmax = celsius_to_temp(85);
		config->def_circuit.limit_wtmin = celsius_to_temp(20);
		config->def_circuit.temp_inoffset = deltaK_to_temp(7);
		
		// DHWT defaults
		config->def_dhwt.limit_wintmax = celsius_to_temp(90);
		config->def_dhwt.limit_tmin = celsius_to_temp(5);
		config->def_dhwt.limit_tmax = celsius_to_temp(60);
		config->def_dhwt.t_comfort = celsius_to_temp(55);
		config->def_dhwt.t_eco = celsius_to_temp(40);
		config->def_dhwt.t_frostfree = celsius_to_temp(10);	// XXX REVISIT RELATIONS BETWEEN TEMPS
		config->def_dhwt.histeresis = deltaK_to_temp(10);
		config->def_dhwt.temp_inoffset = deltaK_to_temp(10);
		
		config->configured = true;
		config_save(config);
	}

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
	boiler->set.idle_mode = IDLE_FROSTONLY;
	boiler->set.histeresis = deltaK_to_temp(8);
	boiler->set.limit_tmax = celsius_to_temp(85);
	boiler->set.limit_tmin = celsius_to_temp(50);
	boiler->set.id_temp = 2;	// XXX VALIDATION
	boiler->set.id_temp_outgoing = boiler->set.id_temp;
	boiler->burner_1 = hardware_relay_new();
	if (!boiler->burner_1) {
		dbgerr("burner relay creation failed");
		return (-EOOM);
	}
	hardware_relay_set_id(boiler->burner_1, 14);	// XXX 2nd relay
	boiler->burner_1->set.configured = true;
	boiler->set.burner_min_time = 2 * 60;	// XXX 2 minutes
	heatsource->set.sleeping_time = 2 * 24 * 60 * 60;	// XXX 2 days
	heatsource->set.consumer_stop_delay = 10 * 60;	// 10mn
	heatsource->set.runmode = RM_AUTO;	// use global setting
	heatsource->set.configured = true;

	// create a new circuit for the plant
	circuit = plant_new_circuit(plant);
	if (!circuit) {
		dbgerr("circuit creation failed");
		return (-EOOM);
	}

	// configure that circuit
	circuit->set.id_temp_outgoing = 3;	// XXX VALIDATION
	circuit->set.id_temp_return = 4;	// XXX VALIDATION
	circuit->tlaw_data.tout1 = celsius_to_temp(-5);
	circuit->tlaw_data.twater1 = celsius_to_temp(67);
	circuit->tlaw_data.tout2 = celsius_to_temp(15);
	circuit->tlaw_data.twater2 = celsius_to_temp(27);
	circuit_make_linear(circuit);

	// create a valve for that circuit
	circuit->valve = plant_new_valve(plant);
	if (!circuit->valve) {
		dbgerr("valve creation failed");
		return (-EOOM);
	}

	// configure that valve
	circuit->valve->set.tdeadzone = deltaK_to_temp(2);
	circuit->valve->set.deadband = 4;	// XXX 4% minimum increments
	circuit->valve->set.ete_time = 120;	// XXX 120 s
	circuit->valve->set.id_temp1 = boiler->set.id_temp_outgoing;
	circuit->valve->set.id_temp2 = circuit->set.id_temp_return;
	circuit->valve->set.id_tempout = circuit->set.id_temp_outgoing;
	valve_make_sapprox(circuit->valve);
	struct s_valve_sapprox_priv * restrict vpriv = circuit->valve->priv;
	
	vpriv->set_sample_intvl = 20;
	vpriv->set_amount = 5;
	vpriv = NULL;
	
	// create and configure two relays for that valve
	circuit->valve->open = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->open, 11);
	circuit->valve->open->set.configured = true;

	circuit->valve->close = hardware_relay_new();
	hardware_relay_set_id(circuit->valve->close, 10);
	circuit->valve->close->set.configured = true;
	
	circuit->valve->set.configured = true;

	// create a pump for that circuit
	circuit->pump = plant_new_pump(plant);
	if (!circuit->pump) {
		dbgerr("pump creation failed");
		return (-EOOM);
	}

	// create and configure a relay for that pump
	circuit->pump->relay = hardware_relay_new();
	hardware_relay_set_id(circuit->pump->relay, 9);
	circuit->pump->relay->set.configured = true;

	circuit->pump->set.configured = true;

	circuit->set.runmode = RM_AUTO;		// use global setting

	circuit->set.configured = true;

	// create a new DHWT for the plant
	dhwt = plant_new_dhwt(plant);
	if (!dhwt) {
		dbgerr("dhwt creation failed");
		return (-EOOM);
	}

	// configure that dhwt
	dhwt->set.id_temp_bottom = boiler->set.id_temp;
	dhwt->set.params.temp_inoffset = deltaK_to_temp(0);	// Integrated tank
	dhwt->set.runmode = RM_AUTO;	// use global setting
	dhwt->set.configured = true;

	plant->configured = true;

	// assign plant to runtime
	runtime->plant = plant;

	// bring the hardware online
	while (hardware_online())
		dbgerr("hardware_online() failed");
	
	// finally bring the runtime online (resets actuators)
	return (runtime_online());
}


static void * thread_master(void *arg)
{
	int ret;
	struct s_runtime * restrict const runtime = get_runtime();
	
	ret = init_process();
	if (ret != ALL_OK) {
		dbgerr("init_proccess failed (%d)", ret);
		abort();	// terminate (and debug) - XXX if this happens the program should not be allowed to continue
	}
	
	// start in frostfree by default
	if (SYS_OFF == get_runtime()->systemmode)
		runtime_set_systemmode(SYS_FROSTFREE);
	
	while (master_thread_sem) {
		hardware_run();
		
		ret = runtime_run();
		if (ret)
			dbgerr("runtime_run returned: %d", ret);
		
		printf("\n");	// XXX DEBUG
		sleep(1);
	}
	
thread_end:
	// cleanup
	dbgmsg("thread exiting!");
	plant_offline(runtime->plant);	// take the plant offline
	config_save(runtime->config);	// save running config before exiting
	hardware_exit();		// stop the hardware - NOTE: this resets the hardware
	plant_del(runtime->plant);	// delete the plant
	config_del(runtime->config);	// delete the config
	pthread_exit(&ret);		// exit
}

int main(void)
{
	struct sigaction saction;
	pthread_t master_thr;
	pthread_attr_t attr;
	const struct sched_param sparam = { RWCHCD_PRIO };
	int ret;

	dbgmsg("Revision %s starting", Version);

	// setup threads
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &sparam);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
	master_thread_sem = 1;

	ret = pthread_create(&master_thr, &attr, thread_master, NULL);
	if (ret)
		errx(ret, "failed to create thread!");

#ifdef _GNU_SOURCE
	pthread_setname_np(master_thr, "master");	// failure ignored
#endif

	// XXX Dropping priviledges here because we need root to set
	// SCHED_FIFO during pthread_create(), and for setname().
	// The thread will run with root credentials for "a little while". REVISIT
	// note: setuid() sends SIG_RT1 to thread due to NPTL implementation
	ret = setgid(RWCHCD_GID);
	if (ret)
		err(ret, "failed to setgid()");
	ret = setuid(RWCHCD_UID);
	if (ret)
		err(ret, "failed to setuid()");
	
	// signal handler for cleanup.
	// No error checking because it's no big deal if it fails
	saction.sa_handler = sig_handler;
	sigemptyset(&saction.sa_mask);
	saction.sa_flags = SA_RESETHAND; // reset default handler after first call
	sigaction(SIGINT, &saction, NULL);
	sigaction(SIGTERM, &saction, NULL);
	
	dbus_main();	// launch dbus main loop, blocks execution until termination
	
	master_thread_sem = 0;	// signal end of work
	pthread_join(master_thr, NULL);	// wait for cleanup
	
	return (0);
}
