//
//  log_file.h
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

#include "storage.h"

int log_file_create(const char * restrict const identifier, const struct s_log_data * const log_data);
int log_file_update(const char * restrict const identifier, const struct s_log_data * const log_data);
void log_file_hook(struct s_log_callbacks * restrict const callbacks);

#endif /* log_file_h */
