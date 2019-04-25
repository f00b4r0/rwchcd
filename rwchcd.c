//
//  rwchcd.c
//  rwchcd
//
//  (C) 2016-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 @mainpage
 rwchcd: a central heating controller daemon for rWCHC.
 
 @author Thibaut VARENE
 @date 2016-2019
 
 Copyright: (C) 2016-2019 Thibaut VARENE
 
 Home page: http://hacks.slashdirt.org/hw/rwchc/
 */

/**
 * @file
 * Main program.
 * @todo:
 * - Auto tuning http://controlguru.com/controller-tuning-using-set-point-driven-data/
 * - connection of multiple instances
 * - multiple heatsources + switchover (e.g. wood furnace -> gas/fuel boiler)
 * @todo cleanup/rationalize _init()/_exit()/_online()/_offline()
 * @todo config reload
 * @todo ^2 fixed-point arithmetic
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
#include "lib.h"
#include "hw_backends.h"
#include "hardware.h"
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
#include "storage.h"
#include "log.h"
#include "timekeep.h"

#include "filecfg.h"

#include "filecfg_parser.tab.h"
extern FILE *filecfg_parser_in;

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

#define RWCHCD_FIFO	"/tmp/rwchcd.fifo"
#define RWCHCD_CONFIG	"/etc/rwchcd.conf"

static volatile bool Sem_master_thread = false;
static volatile bool Sem_master_hwinit_done = false;

static const char Version[] = RWCHCD_REV;	///< RWCHCD_REV is defined in Makefile

static pthread_barrier_t barr_main;

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

/*
 init() initialize blank data structures etc
 online() performs configuration checks and brings subsystem online
 */
static int init_process(void)
{
	int ret;

	/* init hardware backend subsystem - clears data used by config */
	ret = hw_backends_init();
	if (ret) {
		dbgerr("hw_backends init error: %d", ret);
		return (ret);
	}

	/* init runtime - clears data used by config */
	ret = runtime_init();
	if (ret) {
		dbgerr("runtime init error: %d", ret);
		return (ret);
	}

	/* init models - clears data used by config */
	ret = models_init();
	if (ret) {
		dbgerr("models init failed");
		return (ret);
	}

	// this is where we should call the parser
	if (!(filecfg_parser_in = fopen(RWCHCD_CONFIG, "r"))) {
		perror(RWCHCD_CONFIG);
		return (-EGENERIC);
	}
	ret = filecfg_parser_parse();	// XXX REVIEW happens as root
	if (ret)
		return (ret);

	/* init hardware */
	
	ret = hardware_init();		// must happen as root (for SPI access)
	if (ret) {
		dbgerr("hardware init error: %d", ret);
		return (ret);
	}

	// signal hw has been inited
	pthread_barrier_wait(&barr_main);

	// wait for priviledges to be dropped
	pthread_barrier_wait(&barr_main);

	ret = storage_config();
	if (ret) {
		dbgerr("storage config error: %d", ret);
		return (ret);
	}

	ret = log_init();
	if (ret) {
		dbgerr("log config error: %d", ret);
		return (ret);
	}

	/* test and launch */

	// bring the hardware online
	while ((ret = hardware_online()) != ALL_OK) {
		dbgerr("hardware_online() failed: %d", ret);
		timekeep_sleep(1);	// don't pound on the hardware if it's not coming up: calibration data may not be immediately available
	}

	alarms_online();

	// finally bring the runtime online (resets actuators)
	return (runtime_online());
}

static void exit_process(void)
{
	runtime_offline();
	alarms_offline();
	hardware_offline();
	log_exit();
	filecfg_dump();
	hardware_exit();
	models_exit();
	runtime_exit();
	hw_backends_exit();
}

static void * thread_master(void *arg)
{
	int pipewfd = *((int *)arg);
	struct s_runtime * restrict const runtime = runtime_get();
	int ret;
	
	ret = init_process();
	if (ret != ALL_OK) {
		pr_log(_("Process initialization failed (%d) - ABORTING!"), ret);
		abort();	// terminate (and debug) - XXX if this happens the program should not be allowed to continue
	}
	
	// XXX force start in frostfree if OFF by default
	if (SYS_OFF == runtime->systemmode)
		runtime_set_systemmode(SYS_FROSTFREE);
	
	while (Sem_master_thread) {
#ifdef DEBUG
		printf("%ld\n", time(NULL));
#endif

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
	
	// cleanup
	dbgmsg("thread exiting!");
	exit_process();
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

	ret = timekeep_init();
	if (ret)
		errx(ret, "failed to setup timekeeping!");
	
	// setup threads
	pthread_attr_init(&attr);
	pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	pthread_attr_setschedparam(&attr, &sparam);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	// setup main thread barrier
	ret = pthread_barrier_init(&barr_main, NULL, 2);	// 2 threads to sync: master and current
	if (ret)
		errx(ret, "failed to setup main barrier!");

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

	/* wait for hardware init to sync before dropping privilegdges */
	pthread_barrier_wait(&barr_main);

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

	/* signal priviledges have been dropped and fifo is ready */
	pthread_barrier_wait(&barr_main);

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
	pthread_barrier_destroy(&barr_main);
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
	timekeep_exit();

#ifdef DEBUG
	// cleanup fifo
	if (outpipe)
		fclose(outpipe);
	unlink(RWCHCD_FIFO);
#endif

	return (0);
}

