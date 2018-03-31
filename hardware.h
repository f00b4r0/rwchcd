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

#define ON	true
#define OFF	false

struct hardware_callbacks {
	int (*init)(void * priv);	///< must be provided
	int (*online)(void * priv);	///< must be provided
	int (*input)(void * priv);
	int (*output)(void * priv);
	int (*offline)(void * priv);	///< must be provided
	void (*exit)(void * priv);	///< must be provided
	int (*relay_get_state)(void * priv, const rid_t);
	int (*relay_set_state)(void * priv, const rid_t, bool turn_on, time_t change_delay);
	int (*sensor_clone_temp)(void * priv, const sid_t, temp_t * const ctemp);
	int (*sensor_clone_time)(void * priv, const sid_t, time_t * const ctime);
	/* display/alarm ops */
};

int hardware_init(void) __attribute__((warn_unused_result));
int hardware_backend_register(const struct hardware_callbacks * const, void * const priv, const char * const name) __attribute__((warn_unused_result));
int hardware_online(void);
int hardware_input(void);
int hardware_output(void);
int hardware_offline(void);
void hardware_exit(void);

// relay ops
int hardware_relay_get_state(const relid_t);
int hardware_relay_set_state(const relid_t, bool turn_on, time_t change_delay);

// sensor ops
int hardware_sensor_clone_temp(const tempid_t, temp_t * const ctemp) __attribute__((warn_unused_result));
int hardware_sensor_clone_time(const tempid_t, time_t * const clast);

/* display/alarm ops */

#endif /* hardware_h */
