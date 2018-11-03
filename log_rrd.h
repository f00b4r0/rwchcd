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

#include "storage.h"

int log_rrd_create(const char * restrict const identifier, const struct s_log_data * const log_data);
int log_rrd_update(const char * restrict const identifier, const struct s_log_data * const log_data);

#endif /* log_rrd_h */
