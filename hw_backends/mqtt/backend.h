//
//  hw_backends/mqtt/backend.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT backend interface.
 */

#ifndef mqtt_backend_h
#define mqtt_backend_h

#include <mosquitto.h>

#include "rwchcd.h"
#include "hw_backends/hw_backends.h"

/** software representation of an MQTT temperature input */
struct s_mqtt_temperature {
	struct {
		bool configured;	///< true if properly configured
	} set;		///< settings
	struct {
		_Atomic temp_t value;	///< sensor temperature value
		_Atomic timekeep_t tstamp;	///< last update timestamp
	} run;		///< private runtime
	const char * restrict name;	///< @b unique (per backend) user-defined name for the temperature
};

/** software representation of an MQTT switch input */
struct s_mqtt_switch {
	struct {
		bool configured;	///< true if properly configured
	} set;		///< settings
	struct {
		_Atomic bool state;	///< switch state: `true`, `1`, `on`, or `false`, `0`, `off`
		_Atomic timekeep_t tstamp;	///< last update timestamp
	} run;		///< private runtime
	const char * restrict name;	///< @b unique (per backend) user-defined name for the switch
};

/** software representation of an MQTT relay output */
struct s_mqtt_relay {
	struct {
		bool configured;	///< true if properly configured
	} set;		///< settings (externally set)
	const char * restrict name;	///< @b unique (per backend) user-defined name for the relay
};

/** Known temperature units */
enum e_mqtt_tunit {
	MQTT_TEMP_CELSIUS = 0,	///< Subscribed temperatures are expressed in Celsius (*default*). Config `celsius`
	MQTT_TEMP_KELVIN,	///< Subscribed temperatures are expressed in Kelvin. Config `kelvin`
};

/** MQTT backend private data */
struct s_mqtt_pdata {
	struct {
		const char * topic_root;///< MQTT topic root for this backend (used in published and subscribed messages). *REQUIRED*. @note No trailing '/'
		const char * username;	///< MQTT broker username. *Optional*
		const char * password;	///< MQTT broker password. *Optional*
		const char * host;	///< MQTT broker host. *REQUIRED*
		int port;		///< MQTT broker port. *Optional, defaults to 1883*
		enum e_mqtt_tunit temp_unit;	///< temperature unit used in subscribed temperatures. *Optional, defaults to Celsius*
	} set;		///< settings
	struct {
		bool initialized;	///< hardware is initialized (setup() succeeded)
		bool online;		///< hardware is online (online() succeeded)
	} run;		///< private runtime
	struct {
		struct {
			inid_t n;	///< number of allocated temps
			inid_t l;	///< last free temps slot
			struct s_mqtt_temperature * all;	///< pointer to array of temperatures size #n
		} temps;
		struct {
			inid_t n;///< number of allocated switches
			inid_t l;///< last free switch slot
			struct s_mqtt_switch * all;	///< pointer to array of input switches size #n
		} switches;
	} in;
	struct {
		struct {
			outid_t n;	///< number of allocated relays
			outid_t l;	///< last free relay slot
			struct s_mqtt_relay * all;	///< pointer to array of relays size #n
		} rels;
	} out;
	struct mosquitto * mosq;	///< libmosquitto data
	const char * name;		///< user-set name for this backend
};

int mqtt_input_ibn(void * const priv, const enum e_hw_input_type type, const char * const name);
int mqtt_output_ibn(void * const priv, const enum e_hw_output_type type, const char * const name);
int mqtt_backend_register(void * priv, const char * const name);

#endif /* mqtt_backend_h */
