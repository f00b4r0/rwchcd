//
//  hw_backends.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware backends interface API.
 */

#ifndef hw_backends_h
#define hw_backends_h

#include <time.h>

#include "rwchcd.h"

#define HW_MAX_BKENDS	8

/** Hardware backend */
struct s_hw_backend {
	struct {
		bool initialized;	///< true if backend is initialized
		bool online;		///< true if backend is online
	} run;
	const struct s_hw_callbacks * cb;	///< hardware backend callbacks
	void * restrict priv;		///< backend-specific private data
	char * restrict name;	///< backend name
};

/**
 * Backend hardware callbacks.
 * init()/exit()/online()/offline() calls are mandatory.
 * Other calls optional depending on underlying hardware capabilities.
 * All calls take an opaque pointer to implementation-dependent data.
 */
struct s_hw_callbacks {
	int (*init)(void * priv);	///< @warning must be provided
	int (*online)(void * priv);	///< @warning must be provided
	int (*input)(void * priv);	///< reads hardware data
	int (*output)(void * priv);	///< commits data to hardware
	int (*offline)(void * priv);	///< @warning must be provided
	void (*exit)(void * priv);	///< @warning must be provided
	int (*relay_get_state)(void * priv, const rid_t);
	int (*relay_set_state)(void * priv, const rid_t, bool turn_on, time_t change_delay);
	int (*sensor_clone_temp)(void * priv, const sid_t, temp_t * const ctemp);
	int (*sensor_clone_time)(void * priv, const sid_t, time_t * const ctime);
	/* display/alarm ops */
};

extern struct s_hw_backend * HW_backends[HW_MAX_BKENDS];

int hw_backends_init(void);
int hw_backends_register(const struct s_hw_callbacks * const callbacks, void * const priv, const char * const name);
void hw_backends_exit(void);

#endif /* hw_backends_h */
