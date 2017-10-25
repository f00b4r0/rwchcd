//
//  rWCHCd.c
//  A simple daemon for rWCHC
//
//  (C) 2016-2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 @mainpage
 rwchcd: a central heating controller daemon for rWCHC.
 
 @author Thibaut VARENE
 @date 2016-2017
 @copyright GPLv2
 
 Copyright: (C) 2016-2017 Thibaut VARENE
 
 Home page: http://hacks.slashdirt.org/hw/rwchc/
 */

/**
 * @file
 * Main program.
 * @todo:
 * - Setup
 * - Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
 * - UI + dynamic plant creation / setup
 * - Config files
 * - connection of multiple instances
 * - multiple heatsources + switchover (e.g. wood furnace -> gas/fuel boiler)
 * @todo cleanup/rationalize _init()/_exit()/_online()/_offline()
 */

// http://www.energieplus-lesite.be/index.php?id=10963

#include <unistd.h>	// sleep/usleep/setuid
#include <stdlib.h>	// exit
#include <signal.h>
#include <pthread.h>
#include <err.h>
#include <sys/select.h>
#include <errno.h>
#include <sys/types.h>	// fifo
#include <sys/stat.h>
#include <fcntl.h>

#include "rwchcd.h"
#include "lib.h"
#include "hardware.h"
#include "lcd.h"
#include "plant.h"
#include "config.h"
#include "runtime.h"
#include "timer.h"
#include "scheduler.h"
#include "models.h"
#include "alarms.h"
#ifdef HAS_DBUS
 #include "dbus.h"
#endif

#include "pump.h"
#include "valve.h"
#include "circuit.h"
#include "dhwt.h"
#include "heatsource.h"
#include "boiler.h"

#ifndef RWCHCD_PRIO
 #define RWCHCD_PRIO	20	///< Desired run priority
#endif

#ifndef RWCHCD_UID
 #define RWCHCD_UID	65534	///< Desired run uid
#endif

#ifndef RWCHCD_GID
 #define RWCHCD_GID	65534	///< Desired run gid
#endif

#define RWCHCD_WDOGTM	60	///< Watchdog timeout (seconds)

#define RELAY_PUMP	9
#define RELAY_VCLOSE	10
#define RELAY_VOPEN	11
#define RELAY_BURNER	14

#define SENSOR_OUTDOOR	1
#define SENSOR_BURNER	2
#define SENSOR_WATEROUT	3
#define SENSOR_WATERRET	4

static volatile bool Sem_master_thread = false;
static volatile bool Sem_master_hwinit_done = false;

static const char Version[] = RWCHCD_REV;	///< RWCHCD_REV is defined in Makefile

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
#ifdef HAS_DBUS
			dbus_quit();
#else
			Sem_master_thread = false;
#endif
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
	struct s_models * restrict models = NULL;
	struct s_bmodel * restrict bmodel_house = NULL;
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

	Sem_master_hwinit_done = true;

	lcd_init();

	/* init runtime */
	ret = runtime_init();
	if (ret) {
		dbgerr("runtime init error: %d", ret);
		return (ret);
	}

	/* init config */

	config = config_new();
	ret = config_init(config);
	if (ret) {
		dbgerr("config init error: %d", ret);
		return (ret);
	}
	
	if (!config->restored) {
		config_set_temp_nsamples(config, 5);	// XXX 5 samples average
		config_set_nsensors(config, 4);	// XXX 4 sensors
		config_set_outdoor_sensorid(config, SENSOR_OUTDOOR);
		config_set_tsummer(config, celsius_to_temp(18));	// XXX summer switch at 18C
		config_set_tfrost(config, celsius_to_temp(3));		// frost at 3C
		config->summer_maintenance = true;	// enable summer maintenance
		config->sleeping_delay = 1 * 24 * 60 * 60;	// XXX 1 day

		// circuit defaults
		config->def_circuit.t_comfort = celsius_to_temp(20.0F);
		config->def_circuit.t_eco = celsius_to_temp(16);
		config->def_circuit.t_frostfree = celsius_to_temp(7);
		config->def_circuit.outhoff_comfort = config->def_circuit.t_comfort - deltaK_to_temp(2);	// XXX should be deltas and not temps ?
		config->def_circuit.outhoff_eco = config->def_circuit.t_eco - deltaK_to_temp(2);
		config->def_circuit.outhoff_frostfree = config->def_circuit.t_frostfree - deltaK_to_temp(3);	// XXX will trip at t-3 untrip at t-4
		config->def_circuit.outhoff_histeresis = deltaK_to_temp(1);
		config->def_circuit.limit_wtmax = celsius_to_temp(80);
		config->def_circuit.limit_wtmin = celsius_to_temp(20);
		config->def_circuit.temp_inoffset = deltaK_to_temp(5);
		
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

	hardware_sensor_configure(SENSOR_OUTDOOR, ST_PT1000, "outdoor");

	// attach config to runtime
	runtime->config = config;

	/* init models */

	// create new models
	models = models_new();
	if (!models) {
		dbgerr("models creation failed");
		return (-EOOM);
	}

	// create a new building model
	bmodel_house = models_new_bmodel(models, "house");
	if (!bmodel_house) {
		dbgerr("bmodel creation failed");
		return (-EGENERIC);
	}

	bmodel_house->set.tau = 10 * 60 * 60;		// XXX 10 hours
	bmodel_house->set.configured = true;

	models->configured = true;

	// attach models to runtime
	runtime->models = models;

	/* init plant */

	// create a new plant
	plant = plant_new();
	if (!plant) {
		dbgerr("plant creation failed");
		return (-EOOM);
	}

	// create a new heat source for the plant
	heatsource = plant_new_heatsource(plant, "boiler");
	if (!heatsource) {
		dbgerr("heatsource creation failed");
		return (-EOOM);
	}

	// make that heatsource a boiler
	ret = boiler_heatsource(heatsource);
	if (ret)
		return (ret);

	// configure that source	XXX REVISIT
	boiler = heatsource->priv;
	boiler->set.idle_mode = IDLE_FROSTONLY;
	boiler->set.histeresis = deltaK_to_temp(8);
	boiler->set.limit_tmax = celsius_to_temp(90);
	boiler->set.limit_tmin = celsius_to_temp(50);
	ret = hardware_sensor_configure(SENSOR_BURNER, ST_PT1000, "boiler");
	if (ret)
		return ret;
	boiler->set.id_temp = SENSOR_BURNER;
	boiler->set.id_temp_outgoing = boiler->set.id_temp;
	ret = hardware_relay_request(RELAY_BURNER, OFF, "burner");
	if (ret)
		return (ret);
	else
		boiler->set.rid_burner_1 = RELAY_BURNER;
	boiler->set.burner_min_time = 2 * 60;	// XXX 2 minutes
	heatsource->set.consumer_sdelay = 6 * 60;	// 6mn
	heatsource->set.runmode = RM_AUTO;	// use global setting
	heatsource->set.configured = true;

	// create a new circuit for the plant
	circuit = plant_new_circuit(plant, "circuit");
	if (!circuit) {
		dbgerr("circuit creation failed");
		return (-EOOM);
	}

	// configure that circuit
	circuit->set.am_tambient_tK = 60 * 60;	// 1h
	circuit->set.max_boost_time = 60 * 60 * 4;	// 4h
	circuit->set.tambient_boostdelta = deltaK_to_temp(2);	// +2K
	ret = hardware_sensor_configure(SENSOR_WATEROUT, ST_PT1000, "water out");
	if (ret)
		return (ret);
	circuit->set.id_temp_outgoing = SENSOR_WATEROUT;
	ret = hardware_sensor_configure(SENSOR_WATERRET, ST_PT1000, "water return");
	if (ret)
		return (ret);
	circuit->set.id_temp_return = SENSOR_WATERRET;
	circuit_make_bilinear(circuit, celsius_to_temp(-5), celsius_to_temp(66.5F),
			      celsius_to_temp(15), celsius_to_temp(27), 130);

	// create a valve for that circuit
	circuit->valve = plant_new_valve(plant, "valve");
	if (!circuit->valve) {
		dbgerr("valve creation failed");
		return (-EOOM);
	}

	// configure that valve
	circuit->valve->set.tdeadzone = deltaK_to_temp(1);
	circuit->valve->set.deadband = 20;	// XXX 2% minimum increments
	circuit->valve->set.ete_time = 120;	// XXX 120 s
	ret = hardware_sensor_configured(boiler->set.id_temp_outgoing);
	if (ret)
		return (ret);
	circuit->valve->set.id_temp1 = boiler->set.id_temp_outgoing;
	ret = hardware_sensor_configured(circuit->set.id_temp_return);
	if (ret)
		return (ret);
	circuit->valve->set.id_temp2 = circuit->set.id_temp_return;
	ret = hardware_sensor_configured(circuit->set.id_temp_outgoing);
	if (ret)
		return (ret);
	circuit->valve->set.id_tempout = circuit->set.id_temp_outgoing;
	//valve_make_sapprox(circuit->valve, 50, 20);
	valve_make_pi(circuit->valve, 1, 5, 18, deltaK_to_temp(30), 10);
	
	// configure two relays for that valve
	ret = hardware_relay_request(RELAY_VOPEN, OFF, "v_open");
	if (ret)
		return (ret);
	else
		circuit->valve->set.rid_open = RELAY_VOPEN;
	
	ret = hardware_relay_request(RELAY_VCLOSE, OFF, "v_close");
	if (ret)
		return (ret);
	else
		circuit->valve->set.rid_close = RELAY_VCLOSE;
	
	circuit->valve->set.configured = true;

	// create a pump for that circuit
	circuit->pump = plant_new_pump(plant, "pump");
	if (!circuit->pump) {
		dbgerr("pump creation failed");
		return (-EOOM);
	}

	// configure a relay for that pump
	ret = hardware_relay_request(RELAY_PUMP, ON, "pump");
	if (ret)
		return (ret);
	else
		circuit->pump->set.rid_relay = RELAY_PUMP;

	circuit->pump->set.configured = true;

	circuit->set.runmode = RM_AUTO;		// use global setting

	circuit->bmodel = bmodel_house;		// assign building model

	circuit->set.configured = true;

	// create a new DHWT for the plant
	dhwt = plant_new_dhwt(plant, NULL);
	if (!dhwt) {
		dbgerr("dhwt creation failed");
		return (-EOOM);
	}

	// configure that dhwt
	dhwt->set.id_temp_bottom = boiler->set.id_temp;
	dhwt->set.params.temp_inoffset = deltaK_to_temp(0.01F);	// Integrated tank - non-zero because default would take over otherwise
	dhwt->set.runmode = RM_AUTO;	// use global setting
	dhwt->set.configured = true;

	plant->configured = true;

	// assign plant to runtime
	runtime->plant = plant;

	// bring the hardware online
	while (hardware_online())
		dbgerr("hardware_online() failed");

	lcd_online();
	alarms_online();

	// finally bring the runtime online (resets actuators)
	return (runtime_online());
}

static void exit_process(void)
{
	struct s_runtime * restrict const runtime = get_runtime();

	runtime_offline();
	alarms_offline();
	lcd_offline();
	hardware_offline();
	plant_del(runtime->plant);
	models_del(runtime->models);
	config_exit(runtime->config);
	config_del(runtime->config);
	runtime_exit();
	lcd_exit();
	hardware_exit();
}

static void * thread_master(void *arg)
{
	int pipewfd = *((int *)arg);
	struct s_runtime * restrict const runtime = get_runtime();
	int ret;
	
	ret = init_process();
	if (ret != ALL_OK) {
		dbgerr("init_proccess failed (%d)", ret);
		abort();	// terminate (and debug) - XXX if this happens the program should not be allowed to continue
	}
	
	// XXX force start in frostfree if OFF by default
	if (SYS_OFF == runtime->systemmode)
		runtime_set_systemmode(SYS_FROSTFREE);
	
	while (Sem_master_thread) {
		ret = hardware_input();
		if (ret)
			dbgerr("hardware_input returned: %d", ret);
		
		// we lock globally here in this thread. Saves headaches and reduces heavy pressure on the lock
		ret = pthread_rwlock_wrlock(&runtime->runtime_rwlock);
		if (ret)
			dbgerr("wrlock failed: %d", ret);
		
		ret = runtime_run();
		if (ret)
			dbgerr("runtime_run returned: %d", ret);
		
		// we can unlock here
		ret = pthread_rwlock_unlock(&runtime->runtime_rwlock);
		if (ret)
			dbgerr("unlock failed: %d", ret);

		ret = lcd_run();
		if (ret)
			dbgerr("lcd_run failed: %d", ret);
		
		ret = hardware_output();
		if (ret)
			dbgerr("hardware_output returned: %d", ret);
		
		alarms_run();	// XXX run this here last as it clears the alarms

#ifdef DEBUG
		printf("\n");	// insert empty line between each run
#endif
		fflush(stdout);
		
		// send keepalive to watchdog
		/// @warning the loop must run more often than the wdog timeout
		write(pipewfd, " ", 1);	// we don't care what we send
		
		/* this sleep determines the maximum time resolution for the loop,
		 * with significant impact on temp_expw_mavg() and hardware routines. */
		sleep(1);
	}
	
	// cleanup
	dbgmsg("thread exiting!");
	exit_process();
	pthread_exit(NULL);		// exit
}

static void create_schedule(void)
{
	int i;
	
	for (i = 0; i < 7; i++) {
		// every day start comfort at 6:00 and switch to eco at 23:00
		scheduler_add(i, 6, 0, RM_COMFORT, RM_COMFORT);	// comfort at 6:00
		scheduler_add(i, 23, 0, RM_ECO, RM_ECO);	// eco at 23:00
	}
}

/**
 * Simple watchdog thread.
 * Will abort if timeout is reached.
 * @param arg the read end of the pipe set in main()
 * @todo see if pthread_cond_timedwait() wouldn't be more efficient
 */
static void * thread_watchdog(void * arg)
{
	int piperfd = *((int *)arg);
	struct timeval timeout;
	fd_set set;
	int ret, dummy;
	
	FD_ZERO(&set);
	FD_SET(piperfd, &set);
	
	do {
		timeout.tv_sec = RWCHCD_WDOGTM;
		timeout.tv_usec = 0;
		
		ret = select(piperfd+1, &set, NULL, NULL, &timeout);
		if (ret > 0)
			read(piperfd, &dummy, 1);	// empty the pipe; we don't care what we read
		else if ((ret < 0) && (EINTR == errno))
			ret = 1;	// ignore signal interruptions - SA_RESTART doesn't work for select()
	} while (ret > 0);
	
	if (!ret) {// timemout occured
		dbgerr("die!");
		abort();
	}
	else	// ret < 0
		err(ret, NULL);
}

#define RWCHCD_FIFO	"/tmp/rwchcd.fifo"

int main(void)
{
	struct sigaction saction;
	pthread_t master_thr, timer_thr, scheduler_thr, watchdog_thr;
	pthread_attr_t attr;
	const struct sched_param sparam = { RWCHCD_PRIO };
	int pipefd[2];
	int ret;
#ifdef DEBUG
	FILE *outpipe = NULL;
#endif

	pr_log(_("Revision %s starting"), Version);

	// create a pipe for the watchdog
	ret = pipe(pipefd);
	if (ret)
		err(ret, "failed to setup pipe!");
	
	// setup threads
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &sparam);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
	Sem_master_thread = true;

	ret = pthread_create(&master_thr, &attr, thread_master, &pipefd[1]);
	if (ret)
		errx(ret, "failed to create master thread!");

	ret = pthread_create(&watchdog_thr, NULL, thread_watchdog, &pipefd[0]);
	if (ret)
		errx(ret, "failed to create watchdog thread!");

	ret = pthread_create(&timer_thr, NULL, timer_thread, NULL);
	if (ret)
		errx(ret, "failed to create timer thread!");
	
	ret = pthread_create(&scheduler_thr, NULL, scheduler_thread, NULL);
	if (ret)
		errx(ret, "failed to create scheduler thread!");
	
#ifdef _GNU_SOURCE
	pthread_setname_np(master_thr, "master");	// failure ignored
	pthread_setname_np(timer_thr, "timer");
	pthread_setname_np(scheduler_thr, "scheduler");
	pthread_setname_np(watchdog_thr, "watchdog");
#endif

	/* wait for hardware init to complete before dropping priviledges */
	while (!Sem_master_hwinit_done)
		usleep(10000);

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
	
#ifdef DEBUG
	// create the stdout fifo for debugging
	unlink(RWCHCD_FIFO);
	ret = mkfifo(RWCHCD_FIFO, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (!ret) {
		// ignore SIGPIPE
		signal(SIGPIPE, SIG_IGN);

		// redirect stdout
		outpipe = freopen(RWCHCD_FIFO, "w+", stdout);	// open read-write to avoid blocking here

		// make non-blocking
		if (outpipe)
			ret = fcntl(fileno(outpipe), F_SETFL, O_NONBLOCK);

		if (ret)
			abort();	// we can't have a blocking stdout
	}
#endif

	// signal handler for cleanup.
	// No error checking because it's no big deal if it fails
	saction.sa_handler = sig_handler;
	sigemptyset(&saction.sa_mask);
	saction.sa_flags = SA_RESETHAND; // reset default handler after first call
	sigaction(SIGINT, &saction, NULL);
	sigaction(SIGTERM, &saction, NULL);
	
	create_schedule();

#ifdef HAS_DBUS
	dbus_main();	// launch dbus main loop, blocks execution until termination
#else
	pthread_join(master_thr, NULL);	// wait for master end of execution
#endif

	Sem_master_thread = false;	// signal end of work
	pthread_cancel(scheduler_thr);
	pthread_cancel(timer_thr);
	pthread_cancel(watchdog_thr);
	pthread_join(scheduler_thr, NULL);
	pthread_join(timer_thr, NULL);
	pthread_join(watchdog_thr, NULL);
	pthread_join(master_thr, NULL);	// wait for cleanup
	timer_clean_callbacks();
	close(pipefd[0]);
	close(pipefd[1]);

#ifdef DEBUG
	// cleanup fifo
	if (outpipe)
		fclose(outpipe);
	unlink(RWCHCD_FIFO);
#endif

	return (0);
}
