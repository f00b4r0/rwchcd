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

#define FORCE	1
#define NOFORCE	0

struct s_stateful_relay {
	struct {
		bool configured;	///< true if properly configured
		uint_fast8_t id;	///< id matching hardware: 1 to 14, with 13==RL1 and 14==RL2
	} set;		///< settings (externally set)
	struct {
		bool turn_on;		///< state requested by software
		bool is_on;		///< current hardware active state
		time_t on_since;	///< last time on state was triggered, 0 if off
		time_t off_since;	///< last time off state was triggered, 0 if on
		time_t state_time;	///< time spent in current state
		time_t on_tottime;	///< total time spent in on state since system start (updated at state change only)
		time_t off_tottime;	///< total time spent in off state since system start (updated at state change only)
		uint_fast32_t cycles;	///< number of power cycles
	} run;		///< private runtime (internally handled)
	char * restrict name;
};

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

int hardware_init(void) __attribute__((warn_unused_result));
int hardware_config_addr_set(enum e_hw_address address, const int_fast8_t id);
int hardware_config_limit_set(enum e_hw_limit limit, const int_fast8_t value);
int hardware_config_store(void);
int hardware_rwchcrelays_write(void) __attribute__((warn_unused_result));
int hardware_rwchcperiphs_write(void) __attribute__((warn_unused_result));
int hardware_rwchcperiphs_read(void) __attribute__((warn_unused_result));
struct s_stateful_relay * hardware_relay_new(void);
void hardware_relay_del(struct s_stateful_relay * relay);
int hardware_relay_set_id(struct s_stateful_relay * const relay, const uint_fast8_t id) __attribute__((warn_unused_result));
int hardware_relay_set_state(struct s_stateful_relay * relay, bool turn_on, time_t change_delay);
int hardware_relay_get_state(struct s_stateful_relay * const relay);
int hardware_online(void);
int hardware_input(void);
int hardware_output(void);
int hardware_run(void);
int hardware_offline(void);
void hardware_exit(void);

#endif /* rwchcd_hardware_h */
