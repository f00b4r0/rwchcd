//
//  io/hardware.h
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

#include "hw_backends/hw_backends.h"	// for types

// basic ops
int hardware_setup(void) __attribute__((warn_unused_result));
int hardware_online(void) __attribute__((warn_unused_result));
int hardware_input(void) __attribute__((warn_unused_result));
int hardware_output(void) __attribute__((warn_unused_result));
int hardware_offline(void);
void hardware_exit(void);

// relay ops
int hardware_output_state_get(const boutid_t boutid, const enum e_hw_output_type type, u_hw_out_state_t * const state) __attribute__ ((deprecated));
int hardware_output_state_set(const boutid_t boutid, const enum e_hw_output_type type, const u_hw_out_state_t * const state) __attribute__((warn_unused_result));

// sensor ops
int hardware_input_value_get(const binid_t binid, const enum e_hw_input_type type, u_hw_in_value_t * const value) __attribute__((warn_unused_result));
int hardware_input_time_get(const binid_t binid, const enum e_hw_input_type type, timekeep_t * const clast);

/* display/alarm ops */

#endif /* hardware_h */
