//
//  rwchcd_hardware.h
//  
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

/** Hardware addresses available in hw config */
enum e_hw_address {
	HADDR_TBURNER = 1,	///< Burner relay
	HADDR_TPUMP,		///< Pump relay
	HADDR_TVOPEN,		///< Valve open relay
	HADDR_TVCLOSE,		///< Valve close relay
	HADDR_SOUTDOOR,		///< outdoor sensor
	HADDR_SBURNER,		///< burner sensor
	HADDR_SWATER,		///< water sensor
	HADDR_SLAST,		///< last sensor (== number of connected sensors)
};

/** Hardware limits available in hw config */
enum e_hw_limit {
	HLIM_FROSTMIN = 1,	///< Temperature for frost protection
	HLIM_BOILERMAX,		///< Maximum boiler temp
	HLIM_BOILERMIN,		///< Minimum boiler temp
};

/** valid types of temperature sensors */
enum e_sensor_type {
	ST_PT1000,
	ST_NI1000,
	/*	ST_PT100,
	 ST_LGNI1000, */
};

int hardware_init(void) __attribute__((warn_unused_result));
int hardware_config_addr_set(enum e_hw_address address, const relid_t id) __attribute__ ((deprecated));
int hardware_config_limit_set(enum e_hw_limit limit, const int_fast8_t value) __attribute__ ((deprecated));
int hardware_config_store(void);
int hardware_relay_request(const relid_t id, const char * const name) __attribute__((warn_unused_result));
int hardware_relay_release(const relid_t id);
int hardware_relay_set_state(const relid_t id, bool turn_on, time_t change_delay);
int hardware_relay_get_state(const relid_t id);
int hardware_sensor_configure(const tempid_t id, const enum e_sensor_type type, const char * const name) __attribute__((warn_unused_result));
int hardware_sensor_deconfigure(const tempid_t id);
int hardware_sensor_configured(const tempid_t id) __attribute__((warn_unused_result));
int hardware_online(void);
bool hardware_is_online(void);
int hardware_input(void);
int hardware_output(void);
int hardware_run(void);
int hardware_offline(void);
void hardware_exit(void);

#endif /* rwchcd_hardware_h */
