//
//  log/log_rrd.c
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
#include <stdio.h>	// asprintf

#include <rrd.h>

#include "log_rrd.h"
#include "rwchcd.h"

/** Hardcoded RRAs */
static const char *RRAs[] = {
	"RRA:LAST:0.5:1:1w",		// record 1-step samples for 1w
	"RRA:AVERAGE:0.5:15m:1M",	// record 15mn samples for 1M
	"RRA:MIN:0.5:15m:1M",
	"RRA:MAX:0.5:15m:1M",
	"RRA:AVERAGE:0.5:1h:1y",	// record 1h samples for 1y
	"RRA:MIN:0.5:1h:1y",
	"RRA:MAX:0.5:1h:1y",
};

/**
 * Create the RRD log database.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_rrd_create(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	int ret = -EGENERIC, argc = 0;
	unsigned int i, j;
	const char **argv, **rras, * restrict mtype, * str;
	char * temp, dsname[20];
	size_t rrasize;
	static const char * restrict const DSfmt = "DS:%s:%s:%d:U:U";

	assert(identifier && log_data);

	rras = RRAs;
	rrasize = ARRAY_SIZE(RRAs);

	argv = calloc(rrasize + log_data->nkeys, sizeof(*argv));
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
			case LOG_METRIC_IGAUGE:
			case LOG_METRIC_FGAUGE:
				mtype = "GAUGE";
				break;
			case LOG_METRIC_ICOUNTER:
				mtype = "COUNTER";
				break;
			case LOG_METRIC_FCOUNTER:
				mtype = "DCOUNTER";
				break;
			default:
				ret = -EINVALID;
				goto cleanup;
		}

		// replace spaces with '_'. That's the maximum extent of the work we'll do on DS names
		// we also silently truncate to 19 chars which is the maximum allowed length for DS names
		str = log_data->keys[i];
		for (j = 0; ('\0' != str[j]) && (j < ARRAY_SIZE(dsname)-1); j++)
			dsname[j] = (' ' == str[j]) ? '_' : str[j];
		dsname[j] = '\0';

		ret = asprintf(&temp, DSfmt, dsname, mtype, log_data->interval * 4);	// hardcoded: heartbeat: max 4 missed inputs
		if (ret < 0) {
			ret = -EOOM;
			goto cleanup;
		}
		argv[rrasize+i] = temp;
		argc++;
	}

	rrd_clear_error();
	ret = rrd_create_r(identifier, (unsigned)log_data->interval, time(NULL)-10, argc, argv);
	if (ret)
		pr_err("Failed to create RRD data base for \"%s\". Reason: \"%s\"", identifier, rrd_get_error());

cleanup:
	for (i = 0; i < log_data->nkeys; i++)
		freeconst(argv[rrasize+i]);	// cleanup the dynamic DS formats
	freeconst(argv);

	return (ret);
}

/**
 * Update the RRD log database.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_rrd_update(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	char * buffer;
	size_t buffer_len, offset = 0;
	unsigned int i;
	int ret;

	assert(identifier && log_data);

	buffer_len = (24+1) * log_data->nkeys + 1;	// time is 10 chars max, allow 24 (FLT_MANT_DIG) chars per float, plus ':' separator per field, plus '\0'
	buffer = malloc(buffer_len);
	if (!buffer)
		return (-EOOM);

	ret = snprintf(buffer, buffer_len, "%ld", time(NULL));
	offset += (size_t)ret;

	for (i = 0; i < log_data->nvalues; i++) {
		switch (log_data->metrics[i]) {
			case LOG_METRIC_ICOUNTER:
				ret = snprintf(buffer + offset, buffer_len - offset, ":%u", log_data->values[i].u);
				break;
			case LOG_METRIC_IGAUGE:
				ret = snprintf(buffer + offset, buffer_len - offset, ":%d", log_data->values[i].i);
				break;
			case LOG_METRIC_FCOUNTER:
			case LOG_METRIC_FGAUGE:
				ret = snprintf(buffer + offset, buffer_len - offset, ":%f", log_data->values[i].f);
				break;
			default:
				ret = snprintf(buffer + offset, buffer_len - offset, ":U");
				break;
		}

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

	assert(offset < buffer_len);

	rrd_clear_error();
	ret = rrd_update_r(identifier, NULL, 1, &buffer);
	if (ret) {
		dbgerr("%s", rrd_get_error());
	}

cleanup:
	free(buffer);

	return (ret);
}

static const struct s_log_bendcbs log_rrd_cbs = {
	.bkid		= LOG_BKEND_RRD,
	.unversioned	= false,
	.separator	= '_',
	.log_online	= NULL,
	.log_offline	= NULL,
	.log_create	= log_rrd_create,
	.log_update	= log_rrd_update,
};

void log_rrd_hook(const struct s_log_bendcbs ** restrict const callbacks)
{
	assert(callbacks);
	*callbacks = &log_rrd_cbs;
}
