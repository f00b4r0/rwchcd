//
//  log.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log system implementation.
 * @todo REVIEW/CLEANUP
 * @todo configuration support
 */

#include <stdlib.h>	// malloc
#include <string.h>	// strcpy/strcat/strlen
#include <assert.h>

#include "log.h"
#include "storage.h"
#include "log_file.h"
#include "log_rrd.h"
#include "rwchcd.h"
#include "runtime.h"	// to access config->logging
#include "config.h"	// config->logging
#include "timer.h"

#define LOG_PREFIX	"log_"			///< prefix for log names
#define LOG_FMT_SUFFIX	".fmt"			///< suffix for log format names
#define LOG_ASYNC_DUMP_BASENAME	"async_"	///< basename for asynchronous logging (see _log_dump())

/** Log sources linked list */
struct s_log_list {
	const struct s_log_source * lsource;
	struct s_log_list * next;
};

static int log_crawl(const int log_sched_id);

/* wrapper callbacks for interfacing with scheduler_now() */
static int log_crawl_1mn(void)	{ return (log_crawl(LOG_SCHED_1mn)); }
static int log_crawl_5mn(void)	{ return (log_crawl(LOG_SCHED_5mn)); }
static int log_crawl_15mn(void)	{ return (log_crawl(LOG_SCHED_15mn)); }

/**
 * Log schedule array.
 * Must be kept in sync with e_log_sched.
 */
static struct {
	struct s_log_list * loglist;	///< list of log sources
	unsigned int interval;		///< interval in seconds
	timer_cb_t cb;			///< timer callback for that schedule
	const char * name;		///< name of the schedule
} Log_sched[] = {
	{
		// LOG_SCHED_1mn
		.loglist = NULL,
		.interval = LOG_INTVL_1mn,
		.cb = log_crawl_1mn,
		.name = "log_crawl_1mn",
	}, {
		// LOG_SCHED_5mn
		.loglist = NULL,
		.interval = LOG_INTVL_5mn,
		.cb = log_crawl_5mn,
		.name = "log_crawl_5mn",
	}, {
		// LOG_SCHED_15mn
		.loglist = NULL,
		.interval = LOG_INTVL_15mn,
		.cb = log_crawl_15mn,
		.name = "log_crawl_15mn",
	},
};

static bool Log_configured = false;
static struct s_log_bendcbs Log_timed_cb, Log_untimed_cb;

/**
 * Generic log_data log routine.
 * @param basename a namespace under which the unique identifier will be registered
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param log_data the data to log
 * @note uses a #MAX_FILENAMELEN+1 auto heap buffer.
 */
static int _log_dump(const char * restrict const basename, const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	char ident[MAX_FILENAMELEN+1] = LOG_PREFIX;
	const bool logging = runtime_get()->config->logging;
	log_version_t lversion = 0;
	bool fcreate = false, timedlog;
	int ret;
	struct {
		unsigned int nkeys;
		unsigned int nvalues;
		int interval;
		enum e_log_bend bend;
	} logfmt;

	if (!logging)
		return (ALL_OK);

	if (!Log_configured)
		return (-ENOTCONFIGURED);

	if (!basename || !identifier || !version || !log_data)
		return (-EINVALID);

	if (log_data->nvalues > log_data->nkeys)
		return (-EINVALID);

	strcpy(ident + strlen(LOG_PREFIX), basename);
	strcat(ident, identifier);
	strcat(ident, LOG_FMT_SUFFIX);

	timedlog = (log_data->interval > 0) ? true : false;

	ret = storage_fetch(ident, &lversion, &logfmt, sizeof(logfmt));
	if (ALL_OK != ret)
		fcreate = true;
	else {
		// compare with current local version
		if (lversion != *version)
			fcreate = true;

		// compare with current number of keys
		if (logfmt.nkeys != log_data->nkeys)
			fcreate = true;

		// compare with current backend - XXX HACK
		if (logfmt.bend != timedlog ? Log_timed_cb.backend : Log_untimed_cb.backend)
			fcreate = true;
	}

	// strip LOG_FMT_SUFFIX
	ident[strlen(ident) - strlen(LOG_FMT_SUFFIX)] = '\0';

	if (fcreate) {
		// create backend store
		if (timedlog)
			ret = Log_timed_cb.log_create(ident, log_data);
		else
			ret = Log_untimed_cb.log_create(ident, log_data);

		if (ALL_OK != ret)
			return (ret);

		// register new format
		logfmt.nkeys = log_data->nkeys;
		logfmt.nvalues = log_data->nvalues;
		logfmt.interval = log_data->interval;	// XXX do we need this?
		logfmt.bend = timedlog ? Log_timed_cb.backend : Log_untimed_cb.backend;	// XXX HACK

		// XXX reappend LOG_FMT_SUFFIX
		strcat(ident, LOG_FMT_SUFFIX);
		ret = storage_dump(ident, version, &logfmt, sizeof(logfmt));
		if (ALL_OK != ret)
			return (ret);
		// XXX restrip LOG_FMT_SUFFIX
		ident[strlen(ident) - strlen(LOG_FMT_SUFFIX)] = '\0';
	}

	// log data
	if (timedlog)
		ret = Log_timed_cb.log_update(ident, log_data);
	else
		ret = Log_untimed_cb.log_update(ident, log_data);

	return (ret);
}

/**
 * Asynchronously log data.
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param log_data the data to log
 * @warning no collision check on identifier
 */
int log_async_dump(const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	return (_log_dump(LOG_ASYNC_DUMP_BASENAME, identifier, version, log_data));
}

/**
 * Register a scheduled log source.
 * Will insert the provided source into the adequate time-based log list
 * once basic sanity checks are performed.
 * @warning XXX TODO no collision checks performed on basename/identifier
 * @param lsource the log source description
 * @return exec status
 * @todo create log file and log first entry at startup?
 */
int log_register(const struct s_log_source * restrict const lsource)
{
	struct s_log_list * lelmt = NULL, ** currlistp = NULL;
	struct s_log_source * lsrccpy = NULL;
	int ret;

	assert(lsource);

	if (lsource->log_sched >= ARRAY_SIZE(Log_sched)) {
		pr_err(_("Log registration failed: invalid log schedule for %s%s: %d"), lsource->basename, lsource->identifier, lsource->log_sched);
		return (-EINVALID);
	}

	currlistp = &Log_sched[lsource->log_sched].loglist;

	if (!lsource->basename || !lsource->identifier) {
		pr_err(_("Log registration failed: missing basename / identifier"));
		return (-EINVALID);
	}

	// forbid specific namespace
	if (!strcmp(LOG_ASYNC_DUMP_BASENAME, lsource->basename)) {
		pr_err(_("Log registration failed: invalid basename for %s%s: %s"), lsource->basename, lsource->identifier, lsource->basename);
		return (-EINVALID);
	}

	if (!lsource->logdata_cb) {
		pr_err(_("Log registration failed: Missing parser cb: %s%s"), lsource->basename, lsource->identifier);
		return (-EINVALID);
	}

	// object validity is handled by the parser cb

	// check basename + identifier is short enough
	if ((strlen(LOG_PREFIX) + strlen(lsource->basename) + strlen(lsource->identifier) + strlen(LOG_FMT_SUFFIX) + 1) >= MAX_FILENAMELEN) {
		pr_err(_("Log registration failed: Name too long: %s%s"), lsource->basename, lsource->identifier);
		return (-EINVALID);
	}

	lelmt = calloc(1, sizeof(*lelmt));
	if (!lelmt)
		return (-EOOM);

	// copy the passed data for our personal use
	ret = -EOOM;
	lsrccpy = malloc(sizeof(*lsrccpy));
	if (!lsrccpy)
		goto fail;

	memcpy(lsrccpy, lsource, sizeof(*lsrccpy));

	// XXX memory fence
	lelmt->lsource = lsrccpy;
	lelmt->next = *currlistp;
	*currlistp = lelmt;
	// end fence

	dbgmsg("registered \"%s%s\", interval: %d", lsource->basename, lsource->identifier, Log_sched[lsource->log_sched].interval);

	return (ALL_OK);

fail:
	free(lsrccpy);
	free(lelmt);
	return (ret);
}

static void log_clear_listelmt(struct s_log_list * restrict lelmt)
{
	free(lelmt->lsource);
	free(lelmt);
}

/**
 * Deregister a scheduled log source.
 * Will remove the provided source from the corresponding time-based log list.
 * If an object exist the match will be made on object && logdata_cb pointers,
 * otherwise the match will be made on basename/identifier strings.
 * @param lsource the log source description
 * @return exec status (notfound is not reported)
 */
int log_deregister(const struct s_log_source * restrict const lsource)
{
	struct s_log_list * lelmt, * prev = NULL, ** currlistp = NULL;

	assert(lsource);

	if (lsource->log_sched >= ARRAY_SIZE(Log_sched))
		return (-EINVALID);

	currlistp = &Log_sched[lsource->log_sched].loglist;

	// locate element to deregister
	for (lelmt = *currlistp; lelmt; prev = lelmt, lelmt = lelmt->next) {
		// if we have an object, find it in list, and make sure it's associated with the same callback
		if (lsource->object) {
			if ((lelmt->lsource->object == lsource->object) && (lelmt->lsource->logdata_cb == lsource->logdata_cb)) {
				if (!prev)
					*currlistp = lelmt->next;
				else
					prev->next = lelmt->next;
				log_clear_listelmt(lelmt);
				return (ALL_OK);	// stop here
			}
		}
		// otherwise rely on basename/identifier
		else {
			if (!strcmp(lelmt->lsource->identifier, lsource->identifier) && !strcmp(lelmt->lsource->basename, lsource->basename)) {	// match identifier first, assumed to give less false positives
				if (!prev)
					*currlistp = lelmt->next;
				else
					prev->next = lelmt->next;
				log_clear_listelmt(lelmt);
				return (ALL_OK);	// stop here
			}
		}
	}

	dbgmsg("deregistered \"%s%s\"", lsource->basename, lsource->identifier);

	return (ALL_OK);	// failure to find is ignored
}

/**
 * Crawl and process a log source list.
 * @param log_sched_id id for the log schedule to crawl (see #e_log_sched)
 * @return exec status
 */
static int log_crawl(const int log_sched_id)
{
	const struct s_log_list * lelmt;
	const struct s_log_source * lsource;
	struct s_log_data ldata;
	int ret = ALL_OK;

	if (!Log_configured)	// stop crawling when deconfigured
		return (-ENOTCONFIGURED);

	for (lelmt = Log_sched[log_sched_id].loglist; lelmt; lelmt = lelmt->next) {
		lsource = lelmt->lsource;

		lsource->logdata_cb(&ldata, lsource->object);
		ldata.interval = Log_sched[log_sched_id].interval;		// XXX

		ret = _log_dump(lsource->basename, lsource->identifier, &lsource->version, &ldata);
		if (ret)
			dbgmsg("log_dump failed on %s%s: %d", lsource->basename, lsource->identifier, ret);
	}

	return (ret);	// XXX
}

/* Quick hack */
int log_init(void)
{
	int ret;
	unsigned int i;

	if (!storage_isconfigured())
		return (-ENOTCONFIGURED);

	log_file_hook(&Log_untimed_cb);

#ifdef HAS_RRD
	log_rrd_hook(&Log_timed_cb);
#else
	log_file_hook(&Log_timed_cb);
#endif

	Log_configured = true;

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		ret = timer_add_cb(Log_sched[i].interval, Log_sched[i].cb, Log_sched[i].name);
		if (ALL_OK != ret)
			dbgerr("failed to add %s log crawler timer", Log_sched[i].name);	// pr_warn()
	}

	return (ALL_OK);
}

/* idem */
void log_exit(void)
{
	struct s_log_list * lelmt, * next;
	unsigned int i;

	Log_configured = false;

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		for (lelmt = Log_sched[i].loglist; lelmt;) {
			next = lelmt->next;
			log_clear_listelmt(lelmt);
			lelmt = next;
		}
	}
}
