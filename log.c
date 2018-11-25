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

#include "log.h"
#include "storage.h"
#include "log_file.h"
#include "log_rrd.h"
#include "runtime.h"	// to access config->logging
#include "config.h"	// config->logging

static bool Log_configured = false;
static struct s_log_callbacks Log_timed_cb, Log_untimed_cb;

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

	fmtfile = malloc(strlen(identifier) + strlen(".fmt") + 1);
	if (!fmtfile)
		return (-EOOM);

	strcpy(fmtfile, identifier);
	strcat(fmtfile, ".fmt");

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

/** Quick hack */
int log_config(void)
{
	if (!storage_isconfigured())
		return (-ENOTCONFIGURED);

	log_file_hook(&Log_untimed_cb);

#ifdef HAS_RRD
	log_rrd_hook(&Log_timed_cb);
#else
	log_file_hook(&Log_timed_cb);
#endif

	Log_configured = true;

	return (ALL_OK);
}
