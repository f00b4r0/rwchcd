//
//  log/log_rrd.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * RRD log API.
 */

#ifndef log_rrd_h
#define log_rrd_h

#include "log.h"

#define LOG_BKEND_RRD_NAME	"rrd"

void log_rrd_hook(const struct s_log_bendcbs ** restrict const callbacks);

#endif /* log_rrd_h */
