//
//  hw_p1.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 driver API.
 */

#ifndef rwchcd_hw_p1_h
#define rwchcd_hw_p1_h

#include "rwchcd.h"

/** valid types of temperature sensors */
enum e_hw_p1_stype {
	ST_PT1000,
	ST_NI1000,
	/*	ST_PT100,
	 ST_LGNI1000, */
};

void * hw_p1_new(void);

int hw_p1_config_setbl(const uint8_t percent);
int hw_p1_config_setnsensors(const rid_t lastid);
int hw_p1_config_setnsamples(const uint_fast8_t nsamples);

int hw_p1_relay_request(const rid_t id, const bool failstate, const char * const name) __attribute__((warn_unused_result));
int hw_p1_relay_release(const rid_t id);
int hw_p1_sensor_configure(const sid_t id, const enum e_hw_p1_stype type, const temp_t offset, const char * const name) __attribute__((warn_unused_result));
int hw_p1_sensor_deconfigure(const sid_t id);

int hw_p1_backend_register(void * priv, const char * const name);

const char * hw_p1_temp_to_str(const sid_t tempid);

#endif /* rwchcd_hw_p1_h */
