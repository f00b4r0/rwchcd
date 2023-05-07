//
//  log/log.c
//  rwchcd
//
//  (C) 2018-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log system implementation.
 * @todo REVIEW/CLEANUP
 */

#include <stdlib.h>	// malloc
#include <string.h>	// strcpy/strlen
#include <assert.h>

#include "log.h"
#include "storage.h"

#include "rwchcd.h"
#include "timer.h"

#define LOG_PREFIX	"log"			///< prefix for log names
#define LOG_FMT_SUFFIX	".fmt"			///< suffix for log format names
#define MAX_FILENAMELEN	255			///< maximum filename length

/** Log sources linked list */
struct s_log_list {
	const struct s_log_source * lsource;
	log_value_t * vbuffer;			///< pointer to values array of size lsource->nkeys
	struct s_log_list * next;
};

static int log_crawl(const int log_sched_id);

/* wrapper callbacks for interfacing with scheduler_now() */
static int log_crawl_10s(void)	{ return (log_crawl(LOG_SCHED_10s)); }
static int log_crawl_1mn(void)	{ return (log_crawl(LOG_SCHED_1mn)); }
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
		// LOG_SCHED_10s
		.loglist = NULL,
		.interval = LOG_INTVL_10s,
		.cb = log_crawl_10s,
		.name = "log_crawl_10s",
	}, {
		// LOG_SCHED_1mn
		.loglist = NULL,
		.interval = LOG_INTVL_1mn,
		.cb = log_crawl_1mn,
		.name = "log_crawl_1mn",
	}, {
		// LOG_SCHED_15mn
		.loglist = NULL,
		.interval = LOG_INTVL_15mn,
		.cb = log_crawl_15mn,
		.name = "log_crawl_15mn",
	},
};

struct s_log Log;

/**
 * Generic log_data log routine.
 * @param basename a namespace under which the unique identifier will be registered
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param log_data the data to log
 * @return exec status
 * @note uses a #MAX_FILENAMELEN+1 static buffer. Must not be called concurrently
 */
static int _log_dump(const char * restrict const basename, const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	static char ident[MAX_FILENAMELEN+1] = LOG_PREFIX;
	const char sep = Log.bkend->separator;
	const bool unversioned = Log.bkend->unversioned;
	log_version_t lversion = 0;
	bool fcreate = false;
	char * p;
	int ret;
	struct {
		unsigned int nkeys;
		unsigned int nvalues;
		unsigned int interval;
		enum e_log_bend bend;
	} logfmt;

	assert(basename && identifier && version && log_data);

	if (log_data->nvalues > log_data->nkeys)
		return (-EINVALID);

	// log_register() ensures that we cannot overflow
	p = ident + strlen(LOG_PREFIX);
	*p = sep; p++;
	p = stpcpy(p, basename);
	*p = sep; p++;
	p = stpcpy(p, identifier);

	// skip version management for unversioned backends
	if (unversioned)
		goto skip;

	// perform version management
	p = strcpy(p, LOG_FMT_SUFFIX);

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

		// compare with current backend
		if (logfmt.bend != Log.bkend->bkid)
			fcreate = true;

		// compare with current interval
		if (logfmt.interval != log_data->interval)
			fcreate = true;
	}

	// strip LOG_FMT_SUFFIX
	*p = '\0';

	if (unlikely(fcreate)) {
		// create backend store
		ret = Log.bkend->log_create(ident, log_data);

		if (ALL_OK != ret)
			return (ret);

		// register new format
		logfmt.nkeys = log_data->nkeys;
		logfmt.nvalues = log_data->nvalues;	// XXX no need?
		logfmt.interval = log_data->interval;
		logfmt.bend = Log.bkend->bkid;	// XXX HACK

		// reappend LOG_FMT_SUFFIX
		strcpy(p, LOG_FMT_SUFFIX);
		ret = storage_dump(ident, version, &logfmt, sizeof(logfmt));
		if (ALL_OK != ret)
			return (ret);
		// restrip LOG_FMT_SUFFIX
		*p = '\0';
	}

skip:
	// log data
	ret = Log.bkend->log_update(ident, log_data);

	return (ret);
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
	log_value_t * vbuffer = NULL;
	int ret;

	if (!Log.set.enabled)
		return (ALL_OK);	// do nothing

	if (!lsource) {
		pr_err(_("Log registration failed: NULL parameter"));
		return (-EINVALID);
	}

	if (!lsource->version) {
		pr_err(_("Log registration failed: invalid version number for %s %s: %d"), lsource->basename, lsource->identifier, lsource->version);
		return (-EINVALID);
	}

	if (lsource->log_sched >= ARRAY_SIZE(Log_sched)) {
		pr_err(_("Log registration failed: invalid log schedule for %s %s: %d"), lsource->basename, lsource->identifier, lsource->log_sched);
		return (-EINVALID);
	}

	currlistp = &Log_sched[lsource->log_sched].loglist;

	if (!lsource->basename || !lsource->identifier) {
		pr_err(_("Log registration failed: missing basename / identifier"));
		return (-EINVALID);
	}

	if (!lsource->nkeys || !lsource->keys || !lsource->metrics) {
		pr_err(_("Log registration failed: missing keys / metrics"));
		return (-EINVALID);
	}

	if (!lsource->logdata_cb) {
		pr_err(_("Log registration failed: Missing parser cb: %s %s"), lsource->basename, lsource->identifier);
		return (-EINVALID);
	}

	// object validity is handled by the parser cb

	// check basename + identifier is short enough
	if ((strlen(LOG_PREFIX) + strlen(lsource->basename) + 1 + strlen(lsource->identifier) + strlen(LOG_FMT_SUFFIX) + 1) >= MAX_FILENAMELEN) {
		pr_err(_("Log registration failed: Name too long: \"%s %s\""), lsource->basename, lsource->identifier);
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

	// allocate buffer for values
	vbuffer = calloc(lsource->nkeys, sizeof(*vbuffer));
	if (!vbuffer)
		goto fail;

	// XXX memory fence
	lelmt->lsource = lsrccpy;
	lelmt->vbuffer = vbuffer;
	lelmt->next = *currlistp;
	*currlistp = lelmt;
	// end fence

	dbgmsg(1, 1, "registered \"%s %s\", interval: %d", lsource->basename, lsource->identifier, Log_sched[lsource->log_sched].interval);

	return (ALL_OK);

fail:
	free(vbuffer);
	free(lsrccpy);
	free(lelmt);
	return (ret);
}

static void log_clear_listelmt(struct s_log_list * restrict lelmt)
{
	free(lelmt->vbuffer);
	freeconst(lelmt->lsource);
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

	if (!Log.set.enabled)
		return (ALL_OK);	// do nothing

	if (!lsource)
		return (-EINVALID);
	
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

	dbgmsg(1, 1, "deregistered \"%s %s\"", lsource->basename, lsource->identifier);

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

	assert(Log.set.enabled);

	if (!Log.run.online)	// stop crawling when deconfigured
		return (-EOFFLINE);

	for (lelmt = Log_sched[log_sched_id].loglist; lelmt; lelmt = lelmt->next) {
		lsource = lelmt->lsource;

		ldata = (struct s_log_data){
			.nkeys = lsource->nkeys,
			.keys = lsource->keys,
			.metrics = lsource->metrics,
			.values = lelmt->vbuffer,
		};

		ret = lsource->logdata_cb(&ldata, lsource->object);
		if (ALL_OK != ret) {
			dbgerr("logdata_cb failed on %s %s: %d", lsource->basename, lsource->identifier, ret);
			continue;
		}
		ldata.interval = Log_sched[log_sched_id].interval;		// XXX

		ret = _log_dump(lsource->basename, lsource->identifier, &lsource->version, &ldata);
		dbgmsg(1, ret, "log_dump failed on %s %s: %d", lsource->basename, lsource->identifier, ret);
	}

	return (ret);	// XXX
}

/**
 * Init logging subsystem.
 * @return exec status
 */
int log_init(void)
{
	memset(&Log, 0x00, sizeof(Log));

	return (ALL_OK);
}

/**
 * Online logging subsystem.
 * This function tries to bring the configured log backend online and will fail on error.
 * @return exec status
 */
int log_online(void)
{
	int ret;
	unsigned int i;

	if (!Log.set.configured)
		return (-ENOTCONFIGURED);

	if (!Log.set.enabled)
		return (ALL_OK);

	if (!storage_isconfigured())
		pr_warn("Storage not available, logging may not operate correctly.");

	// bring the backends online
	if (Log.bkend->log_online) {
		ret = Log.bkend->log_online();
		if (ALL_OK != ret)
			return (ret);
	}

	Log.run.online = true;

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		ret = timer_add_cb(Log_sched[i].interval, Log_sched[i].cb, Log_sched[i].name);
		if (ALL_OK != ret) {
			dbgerr("failed to add %s log crawler timer", Log_sched[i].name);	// pr_warn()
		}
	}

	return (ALL_OK);
}

/**
 * Offline logging subsystem
 */
int log_offline(void)
{
	if (!Log.set.enabled)
		return (ALL_OK);

	if (!Log.run.online)
		return (-EOFFLINE);

	Log.run.online = false;

	if (Log.bkend->log_offline)
		Log.bkend->log_offline();

	return (ALL_OK);
}

/**
 * Exit logging subsystem.
 * Cleanup all allocated data.
 */
void log_exit(void)
{
	struct s_log_list * lelmt, * next;
	unsigned int i;

	Log.set.configured = false;

	if (Log.bkend && Log.bkend->log_cleanup)
		Log.bkend->log_cleanup();

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		for (lelmt = Log_sched[i].loglist; lelmt;) {
			next = lelmt->next;
			log_clear_listelmt(lelmt);
			lelmt = next;
		}
		Log_sched[i].loglist = NULL;
	}
}
