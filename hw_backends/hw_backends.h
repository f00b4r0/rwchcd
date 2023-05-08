//
//  hw_backends/hw_backends.h
//  rwchcd
//
//  (C) 2018,2020 Thibaut VARENE
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

/** Output state for the supported output types */
union u_hw_out_state {
	bool relay;	///< accessor for #HW_OUTPUT_RELAY state
};

/** Type for hardware output states */
typedef union u_hw_out_state u_hw_out_state_t;

/** Input value for the supported input types */
union u_hw_in_value {
	temp_t temperature;	///< accessor for HW_INPUT_TEMP value
	bool inswitch;		///< accessor for HW_INPUT_SWITCH value
};

/** Type for hardware input states */
typedef union u_hw_in_value u_hw_in_value_t;

/** Known hardware input types */
enum e_hw_input_type {
	HW_INPUT_NONE = 0,	///< input type not configured
	HW_INPUT_TEMP,		///< temperature input
	HW_INPUT_SWITCH,	///< switch input
} ATTRPACK;

/** Known hardware output types */
enum e_hw_output_type {
	HW_OUTPUT_NONE = 0,	///< output type not configured
	HW_OUTPUT_RELAY,	///< relay output
} ATTRPACK;

typedef uint_fast8_t	bid_t;	///< backend idex type - defines theoretical maximum number of backends
typedef uint_fast8_t	inid_t;	///< hardware input index type - defines theoretical maximum number of inputs per backend
typedef uint_fast8_t	outid_t;///< hardware output index type - defines theoretical maximum number of outputs per backend

#define BID_MAX		UINT_FAST8_MAX
#define INID_MAX	UINT_FAST8_MAX
#define OUTID_MAX	UINT_FAST8_MAX

/** backend input id. @note struct assignment is used in the code: must not embed pointers */
typedef struct {
	bid_t bid;	///< backend id
	inid_t inid;	///< input id
} binid_t;
/** backend output id. @note struct assignment is used in the code: must not embed pointers */
typedef struct {
	bid_t bid;	///< backend id
	outid_t outid;	///< output id
} boutid_t;

/**
 * Backend hardware callbacks.
 * These callbacks provide an implementation-agnostic way to access and operate
 * the hardware backends (initialize, access sensors and toggle relays).
 * @note setup()/exit()/online()/offline() calls are mandatory.
 * Other calls optional depending on underlying hardware capabilities.
 * All calls take an opaque pointer to implementation-dependent data.
 */
struct s_hw_callbacks {
	/**
	 * Hardware backend setup callback.
	 * This callback is expected to setup the hardware driver and is executed with @b root privileges.
	 * This is a setup stage that happens immediately after backend configuration and before online().
	 * A delay is applied between the call to this callback and the call to the online() callback,
	 * leaving enough time for the underlying hardware to collect itself.
	 * @warning This callback @b MUST be implemented.
	 * @param priv hardware backend private data
	 * @param name a pointer to a string containing the user-set name for this backend. Guaranteed to be valid until exit() is called.
	 * @return exec status
	 */
	int (*setup)(void * priv, const char * name);

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
	 * @note This callback must release all memory allocated in the @b priv area and freeup all resources.
	 * @param priv hardware backend private data
	 */
	void (*exit)(void * priv);

	/**
	 * Return a backend output name.
	 * @warning if the backend implements @b ANY relay callback, this callback is @b MANDATORY.
	 * @param priv hardware backend private data
	 * @param type the type of requested output
	 * @param oid backend output id
	 * @return target output name or NULL if error
	 */
	const char * (*output_name)(void * const priv, const enum e_hw_output_type type, const outid_t oid);

	/**
	 * Find backend output id by name.
	 * This callback looks up an output in the backend by its name.
	 * @warning if the backend implements @b ANY output callback, this callback is @b MANDATORY.
	 * @warning for a given backend and output type, output names must be unique.
	 * @param priv hardware backend private data
	 * @param type the type of requested output
	 * @param name target output name to look for
	 * @return error if not found or backend output id (must fit outid_t)
	 */
	int (*output_ibn)(void * const priv, const enum e_hw_output_type type, const char * const name);

	/**
	 * Get backend output state.
	 * This callback reads the software representation of the state of an output.
	 * The state returned by this callback accounts for the last
	 * execution of hardware_output(), i.e. the returned state corresponds to
	 * the last enacted hardware state.
	 * Specifically, for example if output_state_set() is called to turn ON a currently OFF
	 * relay, and then output_state_get() is called @b before output() has been
	 * executed, this function will return an OFF state for this relay.
	 * @param priv hardware backend private data
	 * @param type the type of requested output
	 * @param oid backend output id
	 * @param state a pointer to a state location suitable for the target output in which the current state of the output will be stored
	 * @return exec status
	 * @deprecated this callback probably doesn't make much sense in the current code, it isn't used anywhere and might be removed in the future
	 */
	int (*output_state_get)(void * const priv, const enum e_hw_output_type type, const outid_t oid, u_hw_out_state_t * const state);

	/**
	 * Set backend output state.
	 * This callback updates the software representation of the state of an output.
	 * The hardware will reflect the state matching the last call to
	 * this function once hardware_output() has been executed.
	 * @param priv hardware backend private data
	 * @param type the type of requested output
	 * @param oid backend output id
	 * @param state a pointer to a state suitable for the target output
	 * @return exec status
	 */
	int (*output_state_set)(void * const priv, const enum e_hw_output_type type, const outid_t oid, const u_hw_out_state_t * const state);

	/**
	 * Return a backend input name.
	 * @warning if the backend implements @b ANY input callback, this callback is @b MANDATORY.
	 * @param priv hardware backend private data
	 * @param type the type of requested input
	 * @param inid backend input id
	 * @return target input name or NULL if error
	 */
	const char * (*input_name)(void * const priv, const enum e_hw_input_type type, const inid_t inid);

	/**
	 * Find backend input id by name.
	 * This callback looks up an input in the backend by its name.
	 * @warning if the backend implements @b ANY input callback, this callback is @b MANDATORY.
	 * @warning for a given backend and input type, input names must be unique.
	 * @param priv hardware backend private data
	 * @param type the type of requested input
	 * @param name target input name to look for
	 * @return error if not found or backend input id (must fit inid_t)
	 */
	int (*input_ibn)(void * const priv, const enum e_hw_input_type type, const char * const name);

	/**
	 * Get backend input value.
	 * @param priv hardware backend private data
	 * @param type the type of requested input
	 * @param inid backend input id
	 * @param value pointer to a value location suitable for the target input in which the current value of the input will be stored
	 * @return exec status
	 */
	int (*input_value_get)(void * const priv, const enum e_hw_input_type type, const inid_t inid, u_hw_in_value_t * const value);

	/**
	 * Clone sensor update time.
	 * @param priv hardware backend private data
	 * @param type the type of requested input
	 * @param inid backend input id
	 * @param ctime pointer to location where last update time will be stored
	 * @return exec status
	 * @note This function must @b ALWAYS return successfully if the target
	 * sensor is properly configured and the underlying hardware is online.
	 */
	int (*input_time_get)(void * const priv, const enum e_hw_input_type type, const inid_t inid, timekeep_t * const ctime);

	/**
	 * Dump hardware backend configuration.
	 * @param priv hardware backend private data
	 * @return exec status
	 */
	int (*filecfg_dump)(void * priv);

	/* TODO other ops (display/alarm?) */
};

/** Hardware backend */
struct s_hw_backend {
	struct {
		bool initialized;	///< true if backend is initialized
		bool online;		///< true if backend is online
	} run;			///< runtime data
	const struct s_hw_callbacks * cb;	///< hardware backend callbacks
	void * restrict priv;		///< backend-specific private data
	const char * restrict name;	///< unique backend name
};

struct s_hw_backends {
	bid_t n;			///< number of allocated hw backends
	bid_t last;			///< id of last free backend slot
	struct s_hw_backend * all;	///< pointer to array of hw backends of size n
};

int hw_backends_init(void);
int hw_backends_register(const struct s_hw_callbacks * const callbacks, void * const priv, const char * const name);
void hw_backends_exit(void);

int hw_backends_bid_by_name(const char * const name);

#endif /* hw_backends_h */
