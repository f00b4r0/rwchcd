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
 */

#include <stdlib.h>	// malloc
#include <string.h>	// strcpy/strlen
#include <assert.h>

#include "log.h"
#include "storage.h"
#include "log_file.h"
#ifdef HAS_RRD
 #include "log_rrd.h"
#endif
#include "log_statsd.h"

#include "rwchcd.h"
#include "runtime.h"	// to access config->logging
#include "config.h"	// config->logging
#include "timer.h"

#include "filecfg_parser.h"
#include "filecfg.h"

#define LOG_PREFIX	"log"			///< prefix for log names
#define LOG_FMT_SUFFIX	".fmt"			///< suffix for log format names
#define LOG_ASYNC_DUMP_BASENAME	"async"		///< basename for asynchronous logging (see _log_dump())
#define LOG_SEPC	'_'			///< separator character used to concatenate prefix/basename/identifier

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

static struct {
	struct {
		bool configured;			///< true if properly configured (backends are online)
		struct s_log_bendcbs sync_bkend;	///< logging backend for synchronous (periodic) logs. Config expects a user string for backend name.
		struct s_log_bendcbs async_bkend;	///< logging backend for asynchronous logs. Config expects a user string for backend name.
	} set;
} Log;

static struct {
	const char * bkname;		///< backend identifier string (must be unique in system)
	log_bkend_hook_t hook;		///< backend hook routine
	parser_t parse;			///< backend config parser
	void (*dump)(void);		///< backend config dumper. @note if #parse is provided then #dump must be provided too.
	bool configured;		///< true if backend has been configured
} Log_known_bkends[] = {
	{ LOG_BKEND_FILE_NAME,		log_file_hook,		NULL, 				NULL,				false,	},	// file has no configuration
#ifdef HAS_RRD
	{ LOG_BKEND_RRD_NAME,		log_rrd_hook,		NULL,				NULL,				false,	},	// rrd has no configuration
#endif
	{ LOG_BKEND_STATSD_NAME,	log_statsd_hook,	log_statsd_filecfg_parse,	log_statsd_filecfg_dump,	false,	},
};

/**
 * Generic log_data log routine.
 * @param async true if called asynchronously
 * @param basename a namespace under which the unique identifier will be registered
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param log_data the data to log
 * @note uses a #MAX_FILENAMELEN+1 auto heap buffer.
 */
static int _log_dump(const bool async, const char * restrict const basename, const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	char ident[MAX_FILENAMELEN+1] = LOG_PREFIX;
	const bool logging = runtime_get()->config->logging;
	log_version_t lversion = 0;
	bool fcreate = false;
	char * p;
	int ret;
	struct {
		unsigned int nkeys;
		unsigned int nvalues;
		int interval;
		enum e_log_bend bend;
	} logfmt;

	if (!logging)
		return (ALL_OK);

	if (!Log.set.configured)
		return (-ENOTCONFIGURED);

	if (!basename || !identifier || !version || !log_data)
		return (-EINVALID);

	if (log_data->nvalues > log_data->nkeys)
		return (-EINVALID);

	// log_register() ensures that we cannot overflow
	p = ident + strlen(LOG_PREFIX);
	*p = LOG_SEPC; p++;
	p = stpcpy(p, basename);
	*p = LOG_SEPC; p++;
	p = stpcpy(p, identifier);
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
		if (logfmt.bend != async ? Log.set.async_bkend.bkid : Log.set.sync_bkend.bkid)
			fcreate = true;
	}

	// strip LOG_FMT_SUFFIX
	*p = '\0';

	if (fcreate) {
		// create backend store
		if (async)
			ret = Log.set.async_bkend.log_create(async, ident, log_data);
		else
			ret = Log.set.sync_bkend.log_create(async, ident, log_data);

		if (ALL_OK != ret)
			return (ret);

		// register new format
		logfmt.nkeys = log_data->nkeys;
		logfmt.nvalues = log_data->nvalues;
		logfmt.interval = log_data->interval;	// XXX do we need this?
		logfmt.bend = async ? Log.set.async_bkend.bkid : Log.set.sync_bkend.bkid;	// XXX HACK

		// XXX reappend LOG_FMT_SUFFIX
		strcpy(p, LOG_FMT_SUFFIX);
		ret = storage_dump(ident, version, &logfmt, sizeof(logfmt));
		if (ALL_OK != ret)
			return (ret);
		// XXX restrip LOG_FMT_SUFFIX
		*p = '\0';
	}

	// log data
	if (async)
		ret = Log.set.async_bkend.log_update(async, ident, log_data);
	else
		ret = Log.set.sync_bkend.log_update(async, ident, log_data);

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
	return (_log_dump(true, LOG_ASYNC_DUMP_BASENAME, identifier, version, log_data));
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
		pr_err(_("Log registration failed: invalid log schedule for %s %s: %d"), lsource->basename, lsource->identifier, lsource->log_sched);
		return (-EINVALID);
	}

	currlistp = &Log_sched[lsource->log_sched].loglist;

	if (!lsource->basename || !lsource->identifier) {
		pr_err(_("Log registration failed: missing basename / identifier"));
		return (-EINVALID);
	}

	// forbid specific namespace
	if (!strcmp(LOG_ASYNC_DUMP_BASENAME, lsource->basename)) {
		pr_err(_("Log registration failed: invalid basename for %s %s: %s"), lsource->basename, lsource->identifier, lsource->basename);
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

	// XXX memory fence
	lelmt->lsource = lsrccpy;
	lelmt->next = *currlistp;
	*currlistp = lelmt;
	// end fence

	dbgmsg("registered \"%s %s\", interval: %d", lsource->basename, lsource->identifier, Log_sched[lsource->log_sched].interval);

	return (ALL_OK);

fail:
	free(lsrccpy);
	free(lelmt);
	return (ret);
}

static void log_clear_listelmt(struct s_log_list * restrict lelmt)
{
	free((void *)lelmt->lsource);
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

	dbgmsg("deregistered \"%s %s\"", lsource->basename, lsource->identifier);

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

	if (!Log.set.configured)	// stop crawling when deconfigured
		return (-ENOTCONFIGURED);

	for (lelmt = Log_sched[log_sched_id].loglist; lelmt; lelmt = lelmt->next) {
		lsource = lelmt->lsource;

		lsource->logdata_cb(&ldata, lsource->object);
		ldata.interval = Log_sched[log_sched_id].interval;		// XXX

		ret = _log_dump(false, lsource->basename, lsource->identifier, &lsource->version, &ldata);
		if (ret)
			dbgmsg("log_dump failed on %s %s: %d", lsource->basename, lsource->identifier, ret);
	}

	return (ret);	// XXX
}

/**
 * Init logging subsystem.
 * This function tries to bring the configured sync and async backends online and will fail on error.
 * @return exec status
 */
int log_init(void)
{
	int ret;
	unsigned int i;

	if (!storage_isconfigured()) {
		pr_err("Logging needs a configured storage!");
		return (-ENOTCONFIGURED);
	}

	// bring the backends online
	if (Log.set.sync_bkend.log_online) {
		ret = Log.set.sync_bkend.log_online();
		if (ALL_OK != ret)
			return (ret);
	}

	if (Log.set.async_bkend.log_online) {
		ret = Log.set.async_bkend.log_online();
		if (ALL_OK != ret)
			return (ret);
	}

	Log.set.configured = true;

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		ret = timer_add_cb(Log_sched[i].interval, Log_sched[i].cb, Log_sched[i].name);
		if (ALL_OK != ret)
			dbgerr("failed to add %s log crawler timer", Log_sched[i].name);	// pr_warn()
	}

	return (ALL_OK);
}

/**
 * Exit logging subsystem
 */
void log_exit(void)
{
	struct s_log_list * lelmt, * next;
	unsigned int i;

	Log.set.configured = false;

	if (Log.set.sync_bkend.log_offline)
		Log.set.sync_bkend.log_offline();
	if (Log.set.async_bkend.log_offline)
		Log.set.async_bkend.log_offline();

	for (i = 0; i < ARRAY_SIZE(Log_sched); i++) {
		for (lelmt = Log_sched[i].loglist; lelmt;) {
			next = lelmt->next;
			log_clear_listelmt(lelmt);
			lelmt = next;
		}
		Log_sched[i].loglist = NULL;
	}
}

/*
 logging {
	 config {
		 sync_bkend "statsd";
		 async_bkend "file";
	 };
	 backends_conf {
		 backend "statsd" {
			 port "8000";
			 host "localhost";
		 };
	 };
 };
 */

static int log_config_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "sync_bkend", true, NULL, NULL, },		// 0
		{ NODESTR, "async_bkend", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_log_bendcbs * restrict lbkend;
	unsigned int i, j;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	for (j = 0; j < ARRAY_SIZE(parsers); j++) {
		currnode = parsers[j].node;
		switch (j) {
			case 0:
				lbkend = &Log.set.sync_bkend;
				break;
			case 1:
				lbkend = &Log.set.async_bkend;
				break;
			default:
				break;	// can ever happen
		}

		for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
			if (!strcmp(Log_known_bkends[i].bkname, currnode->value.stringval)) {
				Log_known_bkends[i].hook(lbkend);
				break;
			}
		}

		if (ARRAY_SIZE(Log_known_bkends) == i) {
			ret = -EUNKNOWN;
			goto invaliddata;
		}
	}

	return (ALL_OK);
	
invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (ret);
}

static const char * log_config_dump_bkend_name(const struct s_log_bendcbs * restrict const lbkend)
{
	switch (lbkend->bkid) {
		case LOG_BKEND_FILE:
			return (LOG_BKEND_FILE_NAME);
#ifdef HAS_RRD
		case LOG_BKEND_RRD:
			return (LOG_BKEND_RRD_NAME);
#endif
		case LOG_BKEND_STATSD:
			return (LOG_BKEND_STATSD_NAME);
		default:
			return ("unknown");
	}
}

static void log_config_dump(void)
{
	filecfg_iprintf("sync_bkend \"%s\";\n", log_config_dump_bkend_name(&Log.set.sync_bkend));
	filecfg_iprintf("async_bkend \"%s\";\n", log_config_dump_bkend_name(&Log.set.async_bkend));
}

static int log_backend_conf_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
		if (!strcmp(Log_known_bkends[i].bkname, node->value.stringval)) {
			ret = Log_known_bkends[i].parse(priv, node);
			if (ALL_OK == ret)
				Log_known_bkends[i].configured = true;
			return (ret);
		}
	}

	return (-EUNKNOWN);
}

static int log_backends_conf_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", log_backend_conf_parse));
}

/**
 * Parse logging subsystem configuration.
 * The parser expects a mandatory "config" node defining (by name) the sync and async backends to use (see #Log.set).
 * An optional "backends_conf" node can be provided, itself containing named "backend" subnodes detailing
 * the configuration parameters of backends requiring extra configuration.
 * @param priv unused
 * @param node a `logging` node
 * @return exec status
 */
int log_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "config", true, log_config_parse, NULL, },			// 0
		{ NODELST, "backends_conf", false, log_backends_conf_parse, NULL, },
	};
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	return (ret);
}

/**
 * Dump the logging subsystem to config file.
 * @return exec status
 */
int log_filecfg_dump(void)
{
	unsigned int i;

	filecfg_iprintf("logging {\n");
	filecfg_ilevel_inc();

	if (!Log.set.configured)
		goto empty;

	filecfg_iprintf("config {\n");
	filecfg_ilevel_inc();
	log_config_dump();
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");


	// check if we have a configured backend
	for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
		if (Log_known_bkends[i].configured)
			break;
	}

	if (i < ARRAY_SIZE(Log_known_bkends)) {
		filecfg_iprintf("backends_conf {\n");
		filecfg_ilevel_inc();

		for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
			if (Log_known_bkends[i].configured) {
				filecfg_ilevel_inc();
				filecfg_iprintf("backend \"%s\" {\n", Log_known_bkends[i].bkname);
				Log_known_bkends[i].dump();
				filecfg_iprintf("};\n");
				filecfg_ilevel_dec();
			}
		}

		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

empty:
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

