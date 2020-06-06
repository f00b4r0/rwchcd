//
//  filecfg/log_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration dumping.
 */

#include "log_dump.h"
#include "filecfg_dump.h"
#include "log.h"

#include "log_file.h"
#ifdef HAS_RRD
 #include "log_rrd.h"
#endif
#include "log_statsd.h"

extern struct s_log Log;

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

void log_config_dump(void)
{
	filecfg_dump_nodebool("enabled", Log.set.enabled);
	filecfg_dump_nodestr("sync_bkend", log_config_dump_bkend_name(&Log.set.sync_bkend));
	filecfg_dump_nodestr("async_bkend", log_config_dump_bkend_name(&Log.set.async_bkend));
}
