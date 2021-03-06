//
//  log/log_file.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File log API.
 */

#ifndef log_file_h
#define log_file_h

#include "log.h"

#define LOG_BKEND_FILE_NAME	"file"

void log_file_hook(const struct s_log_bendcbs ** restrict const callbacks);

#endif /* log_file_h */
