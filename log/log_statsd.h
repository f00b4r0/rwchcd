//
//  log/log_statsd.h
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
#include "filecfg/parse/filecfg_parser.h"

#define LOG_BKEND_STATSD_NAME	"statsd"

void log_statsd_hook(const struct s_log_bendcbs ** restrict const callbacks);
void log_statsd_filecfg_dump(void);
int log_statsd_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* log_statsd_h */
