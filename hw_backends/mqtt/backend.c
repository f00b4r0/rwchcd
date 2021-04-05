//
//  hw_backends/mqtt/backend.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT backend implementation.
 * The backend exchanges string messages, and uses the mosquitto_loop_start() threaded model.
 * For the time being the backend publishes and subscribes under a single topic root, set in config.
 * It will publish messages for its outputs, and will subscribe to messages for its inputs.
 * Outputs are published when toggled, inputs are updated as received.
 * @warning This backend is a convenience-only implementation. It doesn't implement output coaslescing (i.e. there is no _output() call).
 * For safety reasons discernment shall be applied when using it to interface with inputs, let alone outputs connected to appliances.
 */

#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>	// asprintf
#include <mosquitto.h>

#include "hw_backends/hw_backends.h"
#include "timekeep.h"
#include "lib.h"
#include "backend.h"

#define MQTT_BKND_QOS	2	///< highest QoS level to make sure message are delivered only once

/** Subtopics for outputs. @note Strings should not have common radicals */
static const char * mqtt_outtype_subtopics[] = {
	[HW_OUTPUT_NONE]	= "",	// should never happen
	[HW_OUTPUT_RELAY]	= "relays",
};

/** Subtopics for inputs. @note Strings should not have common radicals */
static const char * mqtt_intype_subtopics[] = {
	[HW_INPUT_NONE]		= "",	// should never happen
	[HW_INPUT_TEMP]		= "temperatures",
	[HW_INPUT_SWITCH]	= "switches",
};

/**
 * Parse a string representing a bool value.
 * This function parses a lowercase string as follows:
 * - Valid "true" values: `1`, `on`, `true`
 * - Valid "false" values: `0`, `off`, `false`
 * @param str the string to parse
 * @return `1` for true values, `0` for false values and negative otherwise
 */
static int mqtt_str_to_bool(const char * str)
{
	int ret = -EINVALID;

	if (!str)
		return (ret);

	switch (*str) {
		case '0':
		case '1':
			if ('\0' == str[1])
				ret = (*str - '0');
			break;
		case 'o':
			switch (str[1]) {
				case 'n':
					if ('\0' == str[2])
						ret = 1;
					break;
				case 'f':
					if (('f' == str[2]) && ('\0' == str[4]))
						ret = 0;
					break;
				default:
					break;
			}
			break;
		case 't':
			if (!strcmp(str, "true"))
				ret = 1;
			break;
		case 'f':
			if (!strcmp(str, "false"))
				ret = 0;
			break;
		default:
			break;
	}

	return (ret);
}

/**
 * MQTT connect callback to subscribe upon connection.
 * This is necessary to correctly handle subscription with unexpected disconnection/reconnection.
 * @param mosq libmosquitto data
 * @param obj MQTT backend private data
 * @param rc connection response return code:
 * 0 - success
 * 1 - connection refused (unacceptable protocol version)
 * 2 - connection refused (identifier rejected)
 * 3 - connection refused (broker unavailable)
 * @warning Subtopics subscription failures are @b only logged. The callback will still try to subscribe to as many subtopics as possible.
 */
static void mqtt_connect_callback(struct mosquitto * mosq, void * obj, int rc)
{
	struct s_mqtt_pdata * restrict const hw = obj;
	unsigned int type;
	char * str;
	int ret;

	if (rc) {
		pr_err("MQTT backend \"%s\": connection failed: \"%s\"", hw->name, mosquitto_connack_string(rc));
		return;
	}

	for (type = 0; type < ARRAY_SIZE(mqtt_intype_subtopics); type++) {
		switch (type) {
			case HW_INPUT_TEMP:
				if (!hw->in.temps.l)
					continue;
				break;
			case HW_INPUT_SWITCH:
				if (!hw->in.switches.l)
					continue;
				break;
			case HW_INPUT_NONE:
			default:
				continue;
		}

		ret = asprintf(&str, "%s/%s/#", hw->set.topic_root, mqtt_intype_subtopics[type]);
		if (ret < 0) {
			pr_err("MQTT backend \"%s\": preparing subtopic \"%s/%s/#\" failed: OUT OF MEMORY!",
			       hw->name, hw->set.topic_root, mqtt_intype_subtopics[type]);
			continue;
		}

		ret = mosquitto_subscribe(mosq, NULL, str, MQTT_BKND_QOS);
		if (ret)
			pr_err("MQTT backend \"%s\": subscription failed for \"%s\": \"%s\"", hw->name, str, mosquitto_strerror(ret));
		free(str);
	}
}

/**
 * MQTT message callback to process incoming messages from subscriptions.
 * @param mosq unused
 * @param obj MQTT backend private data
 * @param message libmosquitto message
 */
static void mqtt_message_callback(struct mosquitto * mosq __attribute__((unused)), void * obj, const struct mosquitto_message * message)
{
	struct s_mqtt_pdata * restrict const hw = obj;
	u_hw_in_value_t u;
	const char *str;
	unsigned int type;
	inid_t id;
	float f;
	int ret;

	// make sure we have a message
	if (message->payloadlen <= 0)
		return;

	// make sure we're interested
	if (strlen(message->topic) <= strlen(hw->set.topic_root))
		return;

	// skip root and slash
	str = message->topic + strlen(hw->set.topic_root) + 1;
	for (type = ARRAY_SIZE(mqtt_intype_subtopics)-1; type; type--) {
		if (!strncmp(str, mqtt_intype_subtopics[type], strlen(mqtt_intype_subtopics[type]))) {
			// match: skip subtopic and break
			str += strlen(mqtt_intype_subtopics[type]);
			break;
		}
	}

	// we expect a topic delimiter here
	if ('/' != *str)
		return;
	str++;

	switch (type) {
		case HW_INPUT_TEMP:
			/// For temperatures we expect a string representing a decimal value compatible with strtof()
			// start with a sanity check:
			f = strtof(message->payload, NULL);
			switch (hw->set.temp_unit) {
				case MQTT_TEMP_CELSIUS:
					u.temperature = celsius_to_temp(f);
					break;
				case MQTT_TEMP_KELVIN:
					u.temperature = kelvin_to_temp(f);
					break;
				default:
					u.temperature = 0;
					break;
			}

			if (validate_temp(u.temperature))
				return;	// invalid value

			// now let's see who that message is for
			ret = mqtt_input_ibn(hw, type, str);
			if (ret < 0)
				return;
			id = (inid_t)ret;

			aser(&hw->in.temps.all[id].run.value, u.temperature);
			aser(&hw->in.temps.all[id].run.tstamp, timekeep_now());
			break;
		case HW_INPUT_SWITCH:
			/// For switches we expect a string representing a boolean values compatible with mqtt_str_to_bool()
			ret = mqtt_str_to_bool(message->payload);
			if (ret < 0)
				return;
			u.inswitch = !!ret;

			ret = mqtt_input_ibn(hw, type, str);
			if (ret < 0)
				return;
			id = (inid_t)ret;

			aser(&hw->in.switches.all[id].run.state, u.inswitch);
			aser(&hw->in.switches.all[id].run.tstamp, timekeep_now());
		case HW_INPUT_NONE:
		default:
			return;	// not for us
	}
}


/**
 * Setup MQTT backend.
 * @param priv private backend data
 * @param name user-set name for this backend
 * @return error state
 */
__attribute__((warn_unused_result)) static int mqtt_setup(void * priv, const char * name)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	int ret;

	if (!hw)
		return (-EINVALID);

	if (!hw->set.host || !hw->set.topic_root)
		return (-EMISCONFIGURED);

	if (!hw->set.port)
		hw->set.port = 1883;

	mosquitto_lib_init();

	hw->mosq = mosquitto_new(NULL, true, hw);
	if (!hw->mosq) {
		ret = -EOOM;
		goto fail;
	}

	ret = mosquitto_username_pw_set(hw->mosq, hw->set.username, hw->set.password);
	if (ret) {
		pr_err("MQTT backend \"%s\": username/password error: \"%s\"", hw->name, mosquitto_strerror(ret));
		ret = -EGENERIC;
		goto fail;
	}

	mosquitto_connect_callback_set(hw->mosq, mqtt_connect_callback);
	mosquitto_message_callback_set(hw->mosq, mqtt_message_callback);

	hw->name = name;
	hw->run.initialized = true;

	return (ALL_OK);

fail:
	mosquitto_destroy(hw->mosq);
	mosquitto_lib_cleanup();
	return (ret);
}

/**
 * Get MQTT backend online.
 * @param priv private backend data
 * @return exec status
 */
static int mqtt_online(void * priv)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	int ret;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.initialized)
		return (-EINIT);

	ret = mosquitto_connect(hw->mosq, hw->set.host, hw->set.port, 60);
	if (ret) {
		pr_err("MQTT backend \"%s\": connect error: \"%s\"", hw->name, mosquitto_strerror(ret));
		return (-EGENERIC);
	}

	// start the network background task
	ret = mosquitto_loop_start(hw->mosq);
	if (ret) {
		pr_err("MQTT backend \"%s\": loop start failed: \"%s\"", hw->name, mosquitto_strerror(ret));
		ret = -EGENERIC;
		goto fail;
	}

	hw->run.online = true;

	return (ALL_OK);

fail:
	mosquitto_disconnect(hw->mosq);
	return (ret);
}

/**
 * Helper to publish output states.
 * @param hw private backend data (for config access)
 * @param type the type of output to publish
 * @param name the name of the output (used in final topic)
 * @param message a string representing the output state
 * @return exec status
 */
static int mqtt_pub_state(const struct s_mqtt_pdata * const hw, enum e_hw_output_type type, const char * restrict const name, const char * restrict const message)
{
	char * restrict topic;
	int ret;

	assert(type < ARRAY_SIZE(mqtt_outtype_subtopics));

	ret = asprintf(&topic, "%s/%s/%s", hw->set.topic_root, mqtt_outtype_subtopics[type], name);
	if (ret < 0)
		return (-EOOM);
	ret = mosquitto_publish(hw->mosq, NULL, topic, strlen(message), message, MQTT_BKND_QOS, false);
	free(topic);
	if (ret) {
		dbgerr("\"%s\" mosquitto_publish failed: \"%s\"", hw->name, mosquitto_strerror(ret));
		return (-EHARDWARE);
	}

	return (ALL_OK);
}

/**
 * Offline MQTT backend.
 * @param priv private backend data
 * @return exec status
 */
static int mqtt_offline(void * priv)
{
	struct s_mqtt_pdata * restrict const hw = priv;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.online)
		return (-EOFFLINE);

	hw->run.online = false;

	mosquitto_disconnect(hw->mosq);
	mosquitto_loop_stop(hw->mosq, true);	// force, possibly due to https://github.com/eclipse/mosquitto/issues/1555

	return (ALL_OK);
}

/**
 * MQTT backend exit routine.
 * @param priv private backend data. Will be invalid after the call.
 */
static void mqtt_exit(void * priv)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	uint_fast8_t id;

	if (!hw)
		return;

	if (hw->run.online) {
		dbgerr("\"%s\" backend is still online!", hw->name);
		return;
	}

	if (!hw->run.initialized)
		return;

	hw->run.initialized = false;

	mosquitto_destroy(hw->mosq);

	mosquitto_lib_cleanup();

	free((void *)hw->set.host);
	free((void *)hw->set.username);
	free((void *)hw->set.password);
	free((void *)hw->set.topic_root);

	for (id = 0; (id < hw->in.temps.l); id++)
		free((void *)hw->in.temps.all[id].name);

	for (id = 0; (id < hw->in.switches.l); id++)
		free((void *)hw->in.switches.all[id].name);

	for (id = 0; (id < hw->out.rels.l); id++)
		free((void *)hw->out.rels.all[id].name);

	free(hw->in.temps.all);
	free(hw->in.switches.all);
	free(hw->out.rels.all);

	free(hw);
}

/**
 * Return MQTT output name.
 * @param priv private backend data
 * @param type the type of requested output
 * @param oid id of the target internal output
 * @return target output name or NULL if error
 */
static const char * mqtt_output_name(void * const priv, const enum e_hw_output_type type, const outid_t oid)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_OUTPUT_RELAY:
			str = (oid >= hw->out.rels.l) ? NULL : hw->out.rels.all[oid].name;
			break;
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * MQTT set output state.
 * @param priv private backend data
 * @param type the type of requested output
 * @param oid id of the internal output to modify
 * @param state pointer to target state of the output
 * @return exec status
 */
static int mqtt_output_state_set(void * const priv, const enum e_hw_output_type type, const outid_t oid, const u_hw_out_state_t * const state)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	union {
		struct s_mqtt_relay * r;
	} u;
	int ret;

	assert(hw && state);

	switch (type) {
		case HW_OUTPUT_RELAY:
			/// For relays he message is either "off" or "on" depending on the target state.
			if (unlikely((oid >= hw->out.rels.l)))
				return (-EINVALID);
			u.r = &hw->out.rels.all[oid];
			if (unlikely(!u.r->set.configured))
				return (-ENOTCONFIGURED);
			ret = mqtt_pub_state(hw, HW_OUTPUT_RELAY, u.r->name, state->relay ? "on" : "off");
			break;
		case HW_OUTPUT_NONE:
		default:
			ret = -EINVALID;
	}

	return (ret);
}

/**
 * Return MQTT input name.
 * @param priv private backend data
 * @param type the type of requested input
 * @param inid id of the target internal input
 * @return target input name or NULL if error
 */
static const char * mqtt_input_name(void * const priv, const enum e_hw_input_type type, const inid_t inid)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_INPUT_TEMP:
			str = (inid >= hw->in.temps.l) ? NULL : hw->in.temps.all[inid].name;
			break;
		case HW_INPUT_SWITCH:
			str = (inid >= hw->in.switches.l) ? NULL : hw->in.switches.all[inid].name;
			break;
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * MQTT get input value.
 * @param priv private backend data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param value location to copy the current value of the input
 * @return exec status
 */
int mqtt_input_value_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, u_hw_in_value_t * const value)
{
	struct s_mqtt_pdata * restrict const hw = priv;
	union {
		struct s_mqtt_temperature * t;
		struct s_mqtt_switch * s;
	} u;

	assert(hw && value);

	switch (type) {
		case HW_INPUT_TEMP:
			if (unlikely((inid >= hw->in.temps.l)))
				return (-EINVALID);
			u.t = &hw->in.temps.all[inid];
			if (unlikely(!u.t->set.configured))
				return (-ENOTCONFIGURED);
			value->temperature = aler(&u.t->run.value);
			break;
		case HW_INPUT_SWITCH:
			if (unlikely((inid >= hw->in.switches.l)))
				return (-EINVALID);
			u.s = &hw->in.switches.all[inid];
			if (unlikely(!u.s->set.configured))
				return (-ENOTCONFIGURED);
			value->inswitch = aler(&u.s->run.state);
			break;
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * MQTT get input last update time.
 * @param priv private backend data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param ctime location to copy the input update time.
 * @return exec status
 */
static int mqtt_input_time_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, timekeep_t * const ctime)
{
	struct s_mqtt_pdata * restrict const hw = priv;

	assert(hw && ctime);

	switch (type) {
		case HW_INPUT_TEMP:
			if (unlikely((inid >= hw->in.temps.l)))
				return (-EINVALID);
			if (unlikely(!hw->in.temps.all[inid].set.configured))
				return (-ENOTCONFIGURED);
			*ctime = aler(&hw->in.temps.all[inid].run.tstamp);
			break;
		case HW_INPUT_SWITCH:
			if (unlikely((inid >= hw->in.switches.l)))
				return (-EINVALID);
			if (unlikely(!hw->in.switches.all[inid].set.configured))
				return (-ENOTCONFIGURED);
			*ctime = aler(&hw->in.switches.all[inid].run.tstamp);
			break;
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Find MQTT input id by name.
 * @param priv private backend data
 * @param type the type of requested input
 * @param name target name to look for
 * @return error if not found or input id
 */
int mqtt_input_ibn(void * const priv, const enum e_hw_input_type type, const char * const name)
{
	const struct s_mqtt_pdata * restrict const hw = priv;
	inid_t id;
	int ret = -ENOTFOUND;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_INPUT_TEMP:
			for (id = 0; (id < hw->in.temps.l); id++) {
				if (!hw->in.temps.all[id].set.configured)
					continue;
				if (!strcmp(hw->in.temps.all[id].name, name)) {
					ret = (int)id;
					break;
				}
			}
			break;
		case HW_INPUT_SWITCH:
			for (id = 0; (id < hw->in.switches.l); id++) {
				if (!hw->in.switches.all[id].set.configured)
					continue;
				if (!strcmp(hw->in.switches.all[id].name, name)) {
					ret = (int)id;
					break;
				}
			}
			break;
		case HW_INPUT_NONE:
		default:
			ret = -EINVALID;
			break;
	}

	return (ret);
}

/**
 * Find MQTT output id by name.
 * @param priv private backend data
 * @param type the type of requested output
 * @param name target name to look for
 * @return error if not found or output id
 */
int mqtt_output_ibn(void * const priv, const enum e_hw_output_type type, const char * const name)
{
	const struct s_mqtt_pdata * restrict const hw = priv;
	outid_t id;
	int ret = -ENOTFOUND;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_OUTPUT_RELAY:
			for (id = 0; (id < hw->out.rels.l); id++) {
				if (!hw->out.rels.all[id].set.configured)
					continue;
				if (!strcmp(hw->out.rels.all[id].name, name)) {
					ret = (int)id;
					break;
				}
			}
			break;
		case HW_OUTPUT_NONE:
		default:
			ret = -EINVALID;
			break;
	}

	return (ret);
}

/** Hardware callbacks for MQTT backend */
static const struct s_hw_callbacks mqtt_callbacks = {
	.setup = mqtt_setup,
	.exit = mqtt_exit,
	.online = mqtt_online,
	.offline = mqtt_offline,
	.input_value_get = mqtt_input_value_get,
	.input_time_get = mqtt_input_time_get,
	.output_state_set = mqtt_output_state_set,
	.input_ibn = mqtt_input_ibn,
	.output_ibn = mqtt_output_ibn,
	.input_name = mqtt_input_name,
	.output_name = mqtt_output_name,
};

/**
 * Backend register wrapper.
 * @param priv private backend data
 * @param name user-defined name
 * @return exec status
 */
int mqtt_backend_register(void * priv, const char * const name)
{
	if (!priv)
		return (-EINVALID);

	return (hw_backends_register(&mqtt_callbacks, priv, name));
}
