//
//  log_statsd.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * StatsD log API.
 */

#ifndef log_statsd_h
#define log_statsd_h

#include "log.h"

void log_statsd_hook(struct s_log_bendcbs * restrict const callbacks);

#endif /* log_statsd_h */
