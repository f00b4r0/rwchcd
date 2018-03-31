//
//  hw_p1.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 interface API.
 */

#ifndef rwchcd_hw_p1_h
#define rwchcd_hw_p1_h

#include <stdbool.h>
#include <time.h>
#include "rwchcd.h"

#define FORCE	true
#define NOFORCE	false

#define ON	true
#define OFF	false

/** valid types of temperature sensors */
enum e_sensor_type {
	ST_PT1000,
	ST_NI1000,
	/*	ST_PT100,
	 ST_LGNI1000, */
};

int hw_p1_init(void) __attribute__((warn_unused_result));
int hw_p1_config_setbl(const uint8_t percent);
int hw_p1_config_setnsensors(const relid_t lastid);
int hw_p1_config_store(void);
int hw_p1_relay_request(const relid_t id, const bool failstate, const char * const name) __attribute__((warn_unused_result));
int hw_p1_relay_release(const relid_t id);
int hw_p1_relay_set_state(const relid_t id, bool turn_on, time_t change_delay);
int hw_p1_relay_get_state(const relid_t id);
int hw_p1_sensor_configure(const tempid_t id, const enum e_sensor_type type, const temp_t offset, const char * const name) __attribute__((warn_unused_result));
int hw_p1_sensor_deconfigure(const tempid_t id);
int hw_p1_sensor_configured(const tempid_t id) __attribute__((warn_unused_result));
int hw_p1_fwversion(void);
int hw_p1_online(void);
bool hw_p1_is_online(void);
int hw_p1_input(void);
int hw_p1_output(void);
int hw_p1_run(void);
int hw_p1_offline(void);
void hw_p1_exit(void);

#endif /* rwchcd_hw_p1_h */