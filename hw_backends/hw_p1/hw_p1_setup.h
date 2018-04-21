//
//  hw_p1_setup.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 setup interface.
 */

#ifndef hw_p1_setup_h
#define hw_p1_setup_h

#include "rwchcd.h"
#include "hw_p1.h"

void * hw_p1_setup_new(void);

int hw_p1_setup_setbl(const uint8_t percent);
int hw_p1_setup_setnsensors(const rid_t lastid);
int hw_p1_setup_setnsamples(const uint_fast8_t nsamples);

int hw_p1_setup_relay_request(const rid_t id, const bool failstate, const char * const name) __attribute__((warn_unused_result));
int hw_p1_setup_relay_release(const rid_t id);
int hw_p1_setup_sensor_configure(const sid_t id, const enum e_hw_p1_stype type, const temp_t offset, const char * const name) __attribute__((warn_unused_result));
int hw_p1_setup_sensor_deconfigure(const sid_t id);

#endif /* hw_p1_setup_h */
