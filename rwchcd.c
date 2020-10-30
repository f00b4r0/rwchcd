//
//  rwchcd.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 @mainpage
 rwchcd: a weather compensated central heating controller daemon.
 
 @author Thibaut VARENE
 @date 2016-2020
 @copyright Copyright (C) 2016-2020 Thibaut VARENE.
 License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html

 Home page: http://hacks.slashdirt.org/sw/rwchcd/

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2, as published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 */

/**
 * @file
 * Main program.
 * @todo
 * - Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
 * - connection of multiple instances
 * - multiple heatsources + switchover (e.g. wood furnace -> gas/fuel boiler)
 * @todo implement a flexible logic system that would take user-definable conditions
 * and user-selectable actions to trigger custom actions (for more flexible plants),
 * for instance the ability to switch from internal boiler DHWT to external electric DHWT
 * (and vice-versa), via control of a zone valve and even optimisation to use all available
 * hot water during switchover; or ability to switch from wood to fuel furnace, etc. Should
 * be implementable via AST.
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
#ifdef DEBUG
 #include <time.h>
#endif

#include "rwchcd.h"
#include "hw_backends/hw_backends.h"
#include "hw_backends/hardware.h"
#include "runtime.h"
#include "timer.h"
#include "scheduler.h"
#include "models.h"
#include "alarms.h"
#ifdef HAS_DBUS
 #include "dbus/dbus.h"
#endif
#include "timekeep.h"

#include "filecfg/dump/filecfg_dump.h"

#include "filecfg/parse/filecfg_parser.tab.h"
extern FILE *filecfg_parser_in;	///< provided and used by the Bison parser

#ifndef RWCHCD_PRIO
 #define RWCHCD_PRIO	20	///< Desired run priority.
#endif

#ifndef RWCHCD_UID
 #define RWCHCD_UID	65534	///< Desired run uid (nobody). Can be overriden via CFLAGS
#endif

#ifndef RWCHCD_GID
 #define RWCHCD_GID	65534	///< Desired run gid (nogroup). Can be overriden via CFLAGS
#endif

#ifndef RWCHCD_WDOGTM
 #define RWCHCD_WDOGTM	60	///< Watchdog timeout (seconds)
#endif

#ifndef RWCHCD_FIFO
 #define RWCHCD_FIFO	"/tmp/rwchcd.fifo"
#endif

#ifndef RWCHCD_CONFIG
 #define RWCHCD_CONFIG	"/etc/rwchcd.conf"	///< Config file location. Can be overriden via CFLAGS
#endif

#ifndef RWCHCD_REV
 #define RWCHCD_REV	"UNDEFINED"		///< Normally defined in Makefile
#endif

static volatile bool Sem_master_thread = false;

static const char Version[] = RWCHCD_REV;	///< Build version string

static struct s_subsys_cb_l {
	const char * name;		///< optional static name of the subsystem
	int (* online)(void);		///< optional online() call for subsystem
	int (* offline)(void);		///< optional offline() call for subsystem
	void (* exit)(void);		///< optional exit() call for subsystem
	struct s_subsys_cb_l * next, * prev;
} * Finish_head = NULL,				///< head of list of subsystem callbacks to execute on program end.
  * Begin_head = NULL;				///< head of list of subsystem callbacks to execute on program start.

/**
 * Add subsystem callbacks.
 * This wrapper adds subsystem-specific online()/offline()/exit() callbacks to be executed during software startup and winddown.
 * @param name the statically allocated name of the subsystem adding callbacks (can be NULL)
 * @param oncb pointer to online() callback (can be NULL)
 * @param offcb pointer to offline() callback (can be NULL)
 * @param exitcb pointer to exit() callback (can be NULL)
 * @return exec status
 * @warning not thread safe
 */
int rwchcd_add_subsyscb(const char * const name, int (* oncb)(void), int (* offcb)(void), void (* exitcb)(void))
{
	struct s_subsys_cb_l * new;

	new = malloc(sizeof(*new));
	if (!new)
		return (-EOOM);

	new->name = name;
	new->online = oncb;
	new->offline = offcb;
	new->exit = exitcb;

	// LIFO for finish: walk from Finish_head via ->prev
	new->next = NULL;
	new->prev = Finish_head;
	if (Finish_head)
		Finish_head->next = new;
	Finish_head = new;

	// FIFO for begin: walk from Begin_head via ->next
	if (!Begin_head)
		Begin_head = new;

	return (ALL_OK);
}

/**
 * Daemon signal handler.
 * - SIGINT, SIGTERM: graceful shutdown.
 * - SIGUSR1: configuration dump.
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
		case SIGUSR1:
			filecfg_dump();
			break;
		default:
			break;
	}
}

/**
 * Initialize data structures and parse configuration.
 * online() performs configuration checks and brings subsystem online
 * @return exec status
 * @note runs as root (due to requirements from e.g. SPI access)
 * @warning MUST NOT RUN IN THREADED CONTEXT (not thread safe)
 */
static int init_process(void)
{
	int ret;

	ret = timekeep_init();
	if (ret) {
		pr_err(_("Failed to setup timekeeping! (%d)"), ret);
		return (ret);
	}

	ret = runtime_init();
	if (ret) {
		pr_err(_("Failed to initialize runtime (%d)"), ret);
		return (ret);
	}

	if (!(filecfg_parser_in = fopen(RWCHCD_CONFIG, "r"))) {
		perror(RWCHCD_CONFIG);
		return (-EGENERIC);
	}
	ret = filecfg_parser_parse();	// XXX REVIEW happens as root
	if (ret) {
		pr_err(_("Configuration parsing failed"));
		return (ret);
	}

	return (ALL_OK);
}

/**
 * Performs configuration checks and brings subsystem online
 * @return exec status
 * @warning MUST NOT RUN IN THREADED CONTEXT (not thread safe)
 */
static int online_subsystems(void)
{
	const struct s_subsys_cb_l * cbs;
	int ret;

	// priviledges could be dropped here
	for (cbs = Begin_head; cbs; cbs = cbs->next) {
		if (cbs->online) {
			ret = cbs->online();
			if (ALL_OK != ret) {
				if (cbs->name)
					pr_err(_("Failed to bring subsystem \"%s\" online (%d)"), cbs->name, ret);
				return (ret);	// fail if any subsystem fails to come online
			}
		}
	}


	// finally bring the runtime online (resets actuators)
	return (runtime_online());
}

/**
 * Reverse operations from online_subsystems().
 * @warning not thread safe.
 */
static void offline_subsystems(void)
{
	const struct s_subsys_cb_l * cbs;
	int ret;

	runtime_offline();	// depends on storage && log && io available [io available depends on hardware]

	for (cbs = Finish_head; cbs; cbs = cbs->prev) {
		if (cbs->offline) {
			ret = cbs->offline();
			if (ret && cbs->name)
				pr_err(_("Failed to bring subsystem \"%s\" offline (%d)"), cbs->name, ret);
			// continue offlining subsystems even if failure
		}
	}

	filecfg_dump();		// [depends on storage]
}

/**
 * Reverse operations from init_process().
 * @warning not thread safe (due to e.g. mosquitto_lib_cleanup()).
 */
static void exit_process(void)
{
	const struct s_subsys_cb_l * cbs;

	timer_clean_callbacks();

	for (cbs = Finish_head; cbs; cbs = cbs->prev) {
		if (cbs->exit)
			cbs->exit();
	}

	runtime_exit();		// depends on nothing

	timekeep_exit();
}

static void * thread_master(void *arg)
{
	int pipewfd = *((int *)arg);
	struct s_runtime * restrict const runtime = runtime_get();
	int ret;

	if (SYS_NONE == runtime->run.systemmode) {	// runtime was not restored
						// set sysmode/runmode from startup config
		ret = runtime_set_systemmode(runtime->set.startup_sysmode);
		if (ALL_OK != ret)
			runtime_set_systemmode(SYS_FROSTFREE);	// fallback to frostfree
		else if (SYS_MANUAL == runtime->set.startup_sysmode) {
			runtime_set_runmode(runtime->set.startup_runmode);
			runtime_set_dhwmode(runtime->set.startup_dhwmode);
		}
	}

	while (Sem_master_thread) {
#ifdef DEBUG
		printf("%ld\n", time(NULL));
#endif

		ret = hardware_input();
		if (ret)
			dbgerr("hardware_input returned: %d", ret);

		ret = models_run();
		if (ALL_OK != ret)
			dbgerr("models_run returned: %d", ret);

		// we lock globally here in this thread. Saves headaches and reduces heavy pressure on the lock
		ret = pthread_rwlock_rdlock(&runtime->runtime_rwlock);
		if (ret)
			dbgerr("wrlock failed: %d", ret);

		ret = runtime_run();
		if (ret)
			dbgerr("runtime_run returned: %d", ret);
		
		// we can unlock here
		ret = pthread_rwlock_unlock(&runtime->runtime_rwlock);
		if (ret)
			dbgerr("unlock failed: %d", ret);

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
		timekeep_sleep(1);
	}

	dbgmsg(1, 1, "thread exiting!");
	pthread_exit(NULL);		// exit
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

int main(void)
{
	struct sigaction saction;
	pthread_t master_thr, timer_thr, scheduler_thr, watchdog_thr, timekeep_thr;
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

	ret = init_process();
	if (ret != ALL_OK) {
		pr_err(_("Process initialization failed (%d) - ABORTING!"), ret);
		abort();	// terminate (and debug) - XXX if this happens the program should not be allowed to continue
	}

	ret = online_subsystems();
	if (ret != ALL_OK) {
		pr_err(_("Subsystems onlining failed (%d) - ABORTING!"), ret);
		abort();	// terminate (and debug) - XXX if this happens the program should not be allowed to continue
	}

	// setup threads
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &sparam);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	Sem_master_thread = true;

	ret = pthread_create(&timekeep_thr, NULL, timekeep_thread, NULL);
	if (ret)
		errx(ret, "failed to create timekeep thread!");

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
	pthread_setname_np(timekeep_thr, "timekeep");
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
	sigaction(SIGUSR1, &saction, NULL);

#ifdef HAS_DBUS
	dbus_main();	// launch dbus main loop, blocks execution until termination
#else
	pthread_join(master_thr, NULL);	// wait for master end of execution
#endif

	Sem_master_thread = false;	// signal end of work
	pthread_cancel(scheduler_thr);
	pthread_cancel(timer_thr);
	pthread_cancel(watchdog_thr);
	pthread_cancel(timekeep_thr);
	pthread_join(scheduler_thr, NULL);
	pthread_join(timer_thr, NULL);
	pthread_join(watchdog_thr, NULL);
	pthread_join(timekeep_thr, NULL);
	pthread_join(master_thr, NULL);	// wait for cleanup
	close(pipefd[0]);
	close(pipefd[1]);

	// cleanup
	offline_subsystems();
	exit_process();

#ifdef DEBUG
	// cleanup fifo
	if (outpipe)
		fclose(outpipe);
	unlink(RWCHCD_FIFO);
#endif

	return (0);
}

