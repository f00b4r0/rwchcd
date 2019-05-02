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

#include "rwchcd.h"
#include "timekeep.h"

#define HW_MAX_BKENDS	8	///< Maximum number of hardware backends allowed

/** Hardware backend */
struct s_hw_backend {
	struct {
		bool initialized;	///< true if backend is initialized
		bool online;		///< true if backend is online
	} run;			///< runtime data
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
	/**
	 * Hardware backend init callback.
	 * This callback is expected to initialize the hardware driver.
	 * Probing/testing hardware presence/communication can be done at this stage if
	 * it doesn't require prior configuration.
	 * @warning This callback @b MUST be implemented.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*init)(void * priv);

	/**
	 * Hardware backend online callback.
	 * When this routine is called the configuration parsing has been performed.
	 * This callback is expected to validate hardware configuration, then apply it
	 * to the hardware and bring it to a suitable state for input()/output() operations.
	 * @warning This callback @b MUST be implemented.
	 * @note if the backend provides sensors, after online() is executed subsequent
	 * calls to sensor_clone_time() MUST succeed (sensor is configured) @b EVEN if
	 * input() hasn't yet been called. This is necessary for other subsystems
	 * online() checks.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*online)(void * priv);

	/**
	 * Hardware backend input callback.
	 * This routine should fetch the current sensor values from the
	 * underlying hardware.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*input)(void * priv);

	/**
	 * Hardware backend output callback.
	 * This routine should commit the computed actuators state to the
	 * underlying hardware.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*output)(void * priv);

	/**
	 * Hardware backend offline callback.
	 * @warning This callback @b MUST be implemented.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*offline)(void * priv);

	/**
	 * Hardware backend exit callback.
	 * @warning This callback @b MUST be implemented.
	 * @note This callback must release all memory allocated in the @b priv area.
	 * @param priv hardware backend private data
	 */
	void (*exit)(void * priv);

	/**
	 * Return a hardware relay name.
	 * @warning if the backend implements @b ANY relay callback, this callback is @b MANDATORY.
	 * @param priv hardware backend private data
	 * @param rid hardware relay id
	 * @return target relay name or NULL if error
	 */
	const char * (*relay_name)(void * priv, const rid_t rid);

	/**
	 * Find hardware relay id by name.
	 * This callback looks up a hardware relay in the backend by its name.
	 * @warning if the backend implements @b ANY relay callback, this callback is @b MANDATORY.
	 * @warning for a given backend, relay names must be unique.
	 * @param priv hardware backend private data
	 * @param name target relay name to look for
	 * @return error if not found or hardware relay id
	 */
	int (*relay_ibn)(void * priv, const char * const name);

	/**
	 * Get relay state.
	 * This callback reads the software representation of the state of a
	 * relay. The state returned by this callback accounts for the last
	 * execution of hardware_output(), i.e. the returned state corresponds to
	 * the last enacted hardware state.
	 * Specifically, if relay_set_state() is called to turn ON a currently OFF
	 * relay, and then relay_get_state() is called @b before output() has been
	 * executed, this function will return an OFF state for this relay.
	 * @param priv hardware backend private data
	 * @param rid hardware relay id
	 * @param turn_on true for turn on request
	 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
	 * @return exec status
	 */
	int (*relay_get_state)(void * priv, const rid_t rid);

	/**
	 * Set relay state.
	 * This callback updates the software representation of the state of a
	 * relay. The hardware will reflect the state matching the last call to
	 * this function after hardware_output() has been executed.
	 * @param priv hardware backend private data
	 * @param rid hardware relay id
	 * @param turn_on true for turn on request
	 * @param change_delay the minimum time the previous running state must be maintained ("cooldown")
	 * @return exec status
	 */
	int (*relay_set_state)(void * priv, const rid_t rid, bool turn_on, timekeep_t change_delay);

	/**
	 * Return a hardware sensor name.
	 * @warning if the backend implements @b ANY sensor callback, this callback is @b MANDATORY.
	 * @param priv hardware backend private data
	 * @param sid hardware sensor id
	 * @return target sensor name or NULL if error
	 */
	const char * (*sensor_name)(void * priv, const sid_t sid);

	/**
	 * Find hardware sensor id by name.
	 * This callback looks up a hardware sensor in the backend by its name.
	 * @warning if the backend implements @b ANY sensor callback, this callback is @b MANDATORY.
	 * @warning for a given backend, sensor names must be unique.
	 * @param priv hardware backend private data
	 * @param name target sensor name to look for
	 * @return error if not found or hardware sensor id
	 */
	int (*sensor_ibn)(void * priv, const char * const name);

	/**
	 * Clone sensor temperature value.
	 * @param priv hardware backend private data
	 * @param sid hardware sendor id
	 * @param ctime optional pointer to allocated space for value storage, can be NULL
	 * @return exec status
	 */
	int (*sensor_clone_temp)(void * priv, const sid_t sid, temp_t * const ctemp);

	/**
	 * Clone sensor update time.
	 * @param priv hardware backend private data
	 * @param sid hardware sendor id
	 * @param ctime optional pointer to allocated space for time storage, can be NULL
	 * @return exec status
	 * @note This function must @b ALWAYS return successfully if the target
	 * sensor is properly configured and the underlying hardware is online.
	 */
	int (*sensor_clone_time)(void * priv, const sid_t sid, timekeep_t * const ctime);

	/**
	 * Dump hardware backend configuration.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*filecfg_dump)(void * priv);

	/* TODO other ops (display/alarm?) */
};

// hardware.c needs access
extern struct s_hw_backend * HW_backends[HW_MAX_BKENDS];

int hw_backends_init(void);
int hw_backends_register(const struct s_hw_callbacks * const callbacks, void * const priv, const char * const name);
int hw_backends_sensor_fbn(tempid_t * tempid, const char * const bkend_name, const char * const sensor_name);
int hw_backends_relay_fbn(relid_t * relid, const char * const bkend_name, const char * const relay_name);
void hw_backends_exit(void);
const char * hw_backends_name(const bid_t bid);

#endif /* hw_backends_h */
