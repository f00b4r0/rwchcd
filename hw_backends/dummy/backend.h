//
//  hw_backends/dummy/backend.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Dummy backend interface.
 */

#ifndef backend_h
#define backend_h

#include "rwchcd.h"
#include "hw_backends/hw_backends.h"

/** software representation of a hardware sensor. */
struct s_dummy_temperature {
	struct {
		bool configured;	///< sensor is configured
		temp_t value;		///< sensor temperature value
	} set;		///< settings
	struct {
	} run;		///< private runtime
	const char * restrict name;	///< @b unique (per backend) user-defined name for the temperature
};

/** software representation of a hardware relay. */
struct s_dummy_relay {
	struct {
		bool configured;	///< true if properly configured
	} set;		///< settings
	struct {
		bool state;		///< state requested by software
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the relay
};

/** dummy backend private data */
struct s_dummy_pdata {
	struct {
	} set;		///< settings
	struct {
		bool initialized;	///< hardware is initialized (init() succeeded)
		bool online;		///< hardware is online (online() succeeded)
	} run;		///< private runtime
	struct {
		struct {
			inid_t n;	///< number of allocated temps
			inid_t l;	///< last free temps slot
			struct s_dummy_temperature * all;	///< pointer to array of temperatures size n
		} temps;
	 in;		///< inputs
	struct {
		struct {
			outid_t n;	///< number of allocated relays
			outid_t l;	///< last free relay slot
			struct s_dummy_relay * all;	///< pointer to array of relays size n
		} rels;
	} out;		///< outputs
};

int dummy_input_ibn(void * const priv, const enum e_hw_input_type type, const char * const name);
int dummy_output_ibn(void * const priv, const enum e_hw_output_type type, const char * const name);
int dummy_backend_register(void * priv, const char * const name);

#endif /* backend_h */
