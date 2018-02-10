//
//  hardware.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware interface API.
 */

#ifndef rwchcd_hardware_h
#define rwchcd_hardware_h

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

int hardware_init(void) __attribute__((warn_unused_result));
int hardware_config_setbl(const uint8_t percent);
int hardware_config_setnsensors(const relid_t lastid);
int hardware_config_store(void);
int hardware_relay_request(const relid_t id, const bool failstate, const char * const name) __attribute__((warn_unused_result));
int hardware_relay_release(const relid_t id);
int hardware_relay_set_state(const relid_t id, bool turn_on, time_t change_delay);
int hardware_relay_get_state(const relid_t id);
int hardware_sensor_configure(const tempid_t id, const enum e_sensor_type type, const temp_t offset, const char * const name) __attribute__((warn_unused_result));
int hardware_sensor_deconfigure(const tempid_t id);
int hardware_sensor_configured(const tempid_t id) __attribute__((warn_unused_result));
int hardware_fwversion(void);
int hardware_online(void);
bool hardware_is_online(void);
int hardware_input(void);
int hardware_output(void);
int hardware_run(void);
int hardware_offline(void);
void hardware_exit(void);

#endif /* rwchcd_hardware_h */
