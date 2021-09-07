//
//  plant/heatsource.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource operation API.
 */

#ifndef heatsource_h
#define heatsource_h

#include "rwchcd.h"

struct s_heatsource;

// XXX cascade
int heatsource_online(struct s_heatsource * const heat) __attribute__((warn_unused_result));
int heatsource_offline(struct s_heatsource * const heat);
int heatsource_request_temp(struct s_heatsource * const heat, const temp_t req);
int heatsource_run(struct s_heatsource * const heat) __attribute__((warn_unused_result));
void heatsource_cleanup(struct s_heatsource * heat);

#endif /* heatsource_h */
