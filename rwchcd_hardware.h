//
//  rwchcd_hardware.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_hardware_h
#define rwchcd_hardware_h

#include <stdbool.h>
#include <time.h>
#include "rwchcd.h"

#define FORCE	1
#define NOFORCE	0

struct s_stateful_relay {
	bool configured;
	unsigned short id;	///< id matching hardware: 1 to 14, with 13==RL1 and 14==RL2
	bool is_on;		///< relay currently active
	time_t on_since;	///< last time on state was triggered, 0 if off XXX these variable should really be handled by the actual hardware call
	time_t on_tottime;	///< total time spent in on state since system start (updated at state change only)
	time_t off_since;	///< last time off state was triggered, 0 if on
	time_t off_tottime;	///< total time spent in off state since system start (updated at state change only)
	time_t state_time;	///< time spent in current state
	unsigned long cycles;	// XXX this should be elswhere (associated with rWCHC_relays) to reflect actual hardware count
	char * restrict name;
};

int hardware_init(void) __attribute__((warn_unused_result));
int hardware_sensors_read(rwchc_sensor_t tsensors[], const int last) __attribute__((warn_unused_result));
int hardware_rwchcrelays_write(const union rwchc_u_relays * const relays) __attribute__((warn_unused_result));
int hardware_rwchcperiphs_write(const union rwchc_u_outperiphs * const periphs) __attribute__((warn_unused_result));
int hardware_rwchcperiphs_read(union rwchc_u_outperiphs * const periphs) __attribute__((warn_unused_result));
temp_t sensor_to_temp(const rwchc_sensor_t raw);
struct s_stateful_relay * hardware_relay_new(void);
void hardware_relay_del(struct s_stateful_relay * relay);
int hardware_relay_set_id(struct s_stateful_relay * const relay, const unsigned short id) __attribute__((warn_unused_result));
int hardware_relay_set_state(struct s_stateful_relay * relay, bool turn_on, time_t change_delay);
int hardware_relay_get_state(struct s_stateful_relay * const relay);

#endif /* rwchcd_hardware_h */
