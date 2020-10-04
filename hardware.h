//
//  hardware.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global hardware interface API.
 */

#ifndef hardware_h
#define hardware_h

#include "rwchcd.h"
#include "timekeep.h"

// basic ops
int hardware_init(void) __attribute__((warn_unused_result));
int hardware_online(void) __attribute__((warn_unused_result));
int hardware_input(void) __attribute__((warn_unused_result));
int hardware_output(void) __attribute__((warn_unused_result));
int hardware_offline(void);
void hardware_exit(void);

// relay ops
int hardware_relay_get_state(const relid_t);
int hardware_relay_set_state(const relid_t, bool turn_on) __attribute__((warn_unused_result));

// sensor ops
int hardware_sensor_clone_temp(const tempid_t, temp_t * const ctemp) __attribute__((warn_unused_result));
int hardware_sensor_clone_time(const tempid_t, timekeep_t * const clast);

/* display/alarm ops */

#endif /* hardware_h */
