//
//  log_rrd.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * RRD log implementation.
 * @warning requires librrd 1.6 or newer
 */

#include <stdlib.h>	// malloc/calloc
#include <time.h>	// time
#include <errno.h>	// errno
#include <string.h>	// strlen
#include <assert.h>

#include <rrd.h>

#include "log_rrd.h"
#include "rwchcd.h"

/* Hardcoded RRAs */
/** 1mn hardcoded RRAs */
static const char *RRAs_1mn[] = {
	"RRA:LAST:0.5:1:2d",		// record 1-step samples for 2d
	"RRA:AVERAGE:0.5:15m:2M",
	"RRA:MIN:0.5:15m:2M",
	"RRA:MAX:0.5:15m:2M",
	"RRA:AVERAGE:0.5:1h:1y",
	"RRA:MIN:0.5:1h:1y",
	"RRA:MAX:0.5:1h:1y",
	/*"RRA:AVERAGE:0.5:1d:10y",	// we really only want 10y for outdoor data
	"RRA:MIN:0.5:1d:10y",
	"RRA:MAX:0.5:1d:10y",*/
};

/** 5mn hardcoded RRAs */
static const char *RRAs_5mn[] = {
	"RRA:LAST:0.5:1:1w",		// record 1-step samples for 1w
	"RRA:AVERAGE:0.5:15m:1M",	// record 15mn samples for 1M
	"RRA:MIN:0.5:15m:1M",
	"RRA:MAX:0.5:15m:1M",
	"RRA:AVERAGE:0.5:1h:1y",	// record 1h samples for 1y
	"RRA:MIN:0.5:1h:1y",
	"RRA:MAX:0.5:1h:1y",
};

/** 15mn hardcoded RRAs */
static const char *RRAs_15mn[] = {
	"RRA:LAST:0.5:1:1M",		// record 1-step samples for 1M
	"RRA:AVERAGE:0.5:1h:1y",	// record 1h samples for 1y
	"RRA:MIN:0.5:1h:1y",
	"RRA:MAX:0.5:1h:1y",
};

/**
 * Create the RRD log database.
 * @param async true if called asynchronously
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_rrd_create(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data)
{
	int ret = -EGENERIC, argc = 0;
	unsigned int i;
	const char **argv, **rras, * restrict mtype;
	char * restrict temp = NULL;
	size_t size, rrasize;
	static const char * restrict const DSfmt = "DS:%s:%s:%d:U:U";

	assert(identifier && log_data);

	switch (log_data->interval) {
		case LOG_INTVL_1mn:
			rras = RRAs_1mn;
			rrasize = ARRAY_SIZE(RRAs_1mn);
			break;
		case LOG_INTVL_5mn:
			rras = RRAs_5mn;
			rrasize = ARRAY_SIZE(RRAs_5mn);
			break;
		case LOG_INTVL_15mn:
			rras = RRAs_15mn;
			rrasize = ARRAY_SIZE(RRAs_15mn);
			break;
		default:
			dbgerr("\"%s\": invalid interval (%d)", identifier, log_data->interval);
			return (-EINVALID);
	}

	argv = calloc(sizeof(*argv), log_data->nkeys + rrasize);
	if (!argv)
		return (-EOOM);

	// prepend RRAs
	for (i = 0; i < rrasize; i++) {
		argv[i] = rras[i];
		argc++;
	}

	// create the DSs
	for (i = 0; i < log_data->nkeys; i++) {
		switch (log_data->metrics[i]) {
			case LOG_METRIC_GAUGE:
				mtype = "GAUGE";
				break;
			case LOG_METRIC_COUNTER:
				mtype = "COUNTER";
				break;
			default:
				ret = -EINVALID;
				goto cleanup;
		}
		snprintf_automalloc(temp, size, DSfmt, log_data->keys[i], mtype, log_data->interval * 4);	// hardcoded: heartbeat: max 4 missed inputs
		if (!temp) {
			ret = -EOOM;
			goto cleanup;
		}
		argv[i+rrasize] = temp;
		argc++;
	}

	rrd_clear_error();
	ret = rrd_create_r(identifier, (unsigned)log_data->interval, time(NULL)-10, argc, argv);
	if (ret)
		dbgerr("%s", rrd_get_error());

cleanup:
	for (i = 0; i < log_data->nkeys; i++)
		free(argv[i+rrasize]);
	free(argv);

	return (ret);
}

/**
 * Update the RRD log database.
 * @param async true if called asynchronously
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_rrd_update(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data)
{
	char * buffer;
	size_t buffer_len, offset = 0;
	unsigned int i;
	int ret;

	assert(identifier && log_data);

	buffer_len = (10+1) * log_data->nkeys + 1;	// INT_MAX is 10 chars, so is time, plus ':' separator, plus '\0'
	buffer = malloc(buffer_len);
	if (!buffer)
		return (-EOOM);

	ret = snprintf(buffer, buffer_len, "%ld", time(NULL));
	offset += (size_t)ret;

	for (i = 0; i < log_data->nvalues; i++) {
		ret = snprintf(buffer + offset, buffer_len - offset, ":%d", log_data->values[i]);
		if ((ret < 0) || ((size_t)ret >= (buffer_len - offset))) {
			ret = -ESTORE;
			goto cleanup;
		}
		offset += (size_t)ret;
	}

	for (i = (log_data->nkeys - log_data->nvalues); i; i--) {
		if ((buffer_len - offset) < 3) {
			ret = -ESTORE;
			goto cleanup;
		}
		strncpy(buffer + offset, ":U", buffer_len - offset);
		offset += strlen(":U");
	}

	rrd_clear_error();
	ret = rrd_update_r(identifier, NULL, 1, &buffer);
	if (ret)
		dbgerr("%s", rrd_get_error());

cleanup:
	free(buffer);

	return (ret);
}

void log_rrd_hook(struct s_log_bendcbs * restrict const callbacks)
{
	assert(callbacks);

	callbacks->bkid = LOG_BKEND_RRD;
	callbacks->unversioned = false;
	callbacks->separator = '_';
	// no online/offline callbacks
	callbacks->log_create = log_rrd_create;
	callbacks->log_update = log_rrd_update;
}
