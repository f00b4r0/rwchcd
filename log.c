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
 */

#include <stdlib.h>	// malloc
#include <string.h>	// strcpy/strcat/strlen
#include <assert.h>

#include "log.h"
#include "storage.h"
#include "log_file.h"
#include "log_rrd.h"
#include "runtime.h"	// to access config->logging
#include "config.h"	// config->logging
#include "timer.h"

#define LOG_PREFIX	"log_"
#define LOG_FMT_SUFFIX	".fmt"

struct s_log_list {
	const struct s_log_source * lsource;
	struct s_log_list * next;
};

static struct s_log_list * Log_l1mn = NULL, * Log_l15mn = NULL, * Log_l1h = NULL;

static bool Log_configured = false;
static struct s_log_bendcbs Log_timed_cb, Log_untimed_cb;

/**
 * Generic log backend keys/values log call.
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param log_data the data to log
 */
int log_dump(const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	const bool logging = runtime_get()->config->logging;
	log_version_t lversion = 0;
	bool fcreate = false, timedlog;
	char *fmtfile;
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

	if (!identifier || !version || !log_data)
		return (-EINVALID);

	if (log_data->nvalues > log_data->nkeys)
		return (-EINVALID);

	fmtfile = malloc(strlen(identifier) + strlen(LOG_FMT_SUFFIX) + 1);
	if (!fmtfile)
		return (-EOOM);

	strcpy(fmtfile, identifier);
	strcat(fmtfile, LOG_FMT_SUFFIX);

	timedlog = (log_data->interval > 0) ? true : false;

	ret = storage_fetch(fmtfile, &lversion, &logfmt, sizeof(logfmt));
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
		if (logfmt.bend != timedlog ? Log_timed_cb.backend : Log_untimed_cb.backend)
			fcreate = true;
	}

	if (fcreate) {
		// create backend store
		if (timedlog)
			ret = Log_timed_cb.log_create(identifier, log_data);
		else
			ret = Log_untimed_cb.log_create(identifier, log_data);

		if (ALL_OK != ret)
			goto cleanup;

		// register new format
		logfmt.nkeys = log_data->nkeys;
		logfmt.nvalues = log_data->nvalues;
		logfmt.interval = log_data->interval;
		logfmt.bend = timedlog ? Log_timed_cb.backend : Log_untimed_cb.backend;
		ret = storage_dump(fmtfile, version, &logfmt, sizeof(logfmt));
		if (ALL_OK != ret)
			goto cleanup;
	}

	// log data
	if (timedlog)
		ret = Log_timed_cb.log_update(identifier, log_data);
	else
		ret = Log_untimed_cb.log_update(identifier, log_data);

cleanup:
	free(fmtfile);
	return (ret);
}

/**
 * Register a scheduled log source.
 * Will insert the provided source into the adequate time-based log list
 * once basic sanity checks are performed.
 * @warning XXX TODO no collision checks performed on basename/identifier
 * @param lsource the log source description
 * @return exec status
 */
int log_register(const struct s_log_source * restrict const lsource)
{
	struct s_log_list * lelmt = NULL, ** currlistp = NULL;
	struct s_log_source * lsrccpy = NULL;
	int ret;

	assert(lsource);

	switch (lsource->interval) {
		case LOG_INTVL_1mn:
			currlistp = &Log_l1mn;
			break;
		case LOG_INTVL_15mn:
			currlistp = &Log_l15mn;
			break;
		case LOG_INTVL_1h:
			currlistp = &Log_l1h;
			break;
		default:
			dbgerr("Invalid log interval for %s%s: %d", lsource->basename, lsource->identifier, lsource->interval);
			return (-EINVALID);
	}

	if (!lsource->basename || !lsource->identifier) {
		dbgerr("Missing basename / identifier");
		return (-EINVALID);
	}

	if (!lsource->logdata_cb) {
		dbgerr("Missing parser cb: %s%s", lsource->basename, lsource->identifier);
		return (-EINVALID);
	}

	// object validity is handled by the parser cb

	// check basename + identifier is short enough
	if ((strlen(LOG_PREFIX) + strlen(lsource->basename) + strlen(lsource->identifier) + strlen(LOG_FMT_SUFFIX) + 1) >= 255) {
		dbgerr("Name too long: %s%s", lsource->basename, lsource->identifier);
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

	dbgmsg("registered \"%s%s\", interval: %d", lsource->basename, lsource->identifier, lsource->interval);

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
 * @param lsource the log source description
 * @return exec status (notfound is not reported)
 */
int log_deregister(const struct s_log_source * restrict const lsource)
{
	struct s_log_list * lelmt, * prev = NULL, ** currlistp = NULL;

	assert(lsource);

	switch (lsource->interval) {
		case LOG_INTVL_1mn:
			currlistp = &Log_l1mn;
			break;
		case LOG_INTVL_15mn:
			currlistp = &Log_l15mn;
			break;
		case LOG_INTVL_1h:
			currlistp = &Log_l1h;
			break;
		default:
			return (-EINVALID);
	}

	// locate element to deregister
	for (lelmt = *currlistp; lelmt; prev = lelmt, lelmt = lelmt->next) {
		// if we have an object, find it in list
		if (lsource->object) {
			if (lelmt->lsource->object == lsource->object) {
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
 * @param list the list of log sources to crawl
 * @return exec status
 */
static int log_crawl(const struct s_log_list * restrict const list)
{
	static char ident[256] = LOG_PREFIX;
	const struct s_log_list * lelmt;
	const struct s_log_source * lsource;
	struct s_log_data ldata;
	int ret = ALL_OK;

	if (!Log_configured)	// stop crawling when deconfigured
		return (-ENOTCONFIGURED);

	for (lelmt = list; lelmt; lelmt = lelmt->next) {
		lsource = lelmt->lsource;
		strcpy(ident + strlen(LOG_PREFIX), lsource->basename);
		strcat(ident, lsource->identifier);

		lsource->logdata_cb(&ldata, lsource->object);
		ldata.interval = lsource->interval;		// XXX

		ret = log_dump(ident, &lsource->version, &ldata);
		if (ret)
			dbgmsg("log_dump failed on %s%s: %d", lsource->basename, lsource->identifier, ret);
	}

	return (ret);	// XXX
}

static int log_crawl_1mn(void)
{
	return (log_crawl(Log_l1mn));
}

static int log_crawl_15mn(void)
{
	return (log_crawl(Log_l15mn));
}

static int log_crawl_1h(void)
{
	return (log_crawl(Log_l1h));
}

/* Quick hack */
int log_init(void)
{
	int ret;

	if (!storage_isconfigured())
		return (-ENOTCONFIGURED);

	log_file_hook(&Log_untimed_cb);

#ifdef HAS_RRD
	log_rrd_hook(&Log_timed_cb);
#else
	log_file_hook(&Log_timed_cb);
#endif

	Log_configured = true;

	ret = timer_add_cb(LOG_INTVL_1mn, log_crawl_1mn, "log_crawl_1mn");
	if (ALL_OK != ret)
		dbgerr("failed to add 1mn log crawler timer");

	timer_add_cb(LOG_INTVL_15mn, log_crawl_15mn, "log_crawl_15mn");
	if (ALL_OK != ret)
		dbgerr("failed to add 15mn log crawler timer");

	timer_add_cb(LOG_INTVL_1h, log_crawl_1h, "log_crawl_1h");
	if (ALL_OK != ret)
		dbgerr("failed to add 1h log crawler timer");

	return (ALL_OK);
}

/* idem */
void log_exit(void)
{
	struct s_log_list * lelmt, * next;

	Log_configured = false;

	for (lelmt = Log_l1mn; lelmt;) {
		next = lelmt->next;
		log_clear_listelmt(lelmt);
		lelmt = next;
	}

	for (lelmt = Log_l15mn; lelmt;) {
		next = lelmt->next;
		log_clear_listelmt(lelmt);
		lelmt = next;
	}

	for (lelmt = Log_l1h; lelmt;) {
		next = lelmt->next;
		log_clear_listelmt(lelmt);
		lelmt = next;
	}
}
