//
//  log_rrd.h
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

void log_rrd_hook(struct s_log_bendcbs * restrict const callbacks);

#endif /* log_rrd_h */
