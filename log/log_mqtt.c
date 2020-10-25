//
//  log/log_mqtt.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT log implementation, using Mosquitto.
 * @warning no checks are provided to avoid doing something stupid such as overlapping the MQTT hw_backend topic space.
 */

#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <mosquitto.h>

#include "log_mqtt.h"
#include "rwchcd.h"
#include "filecfg/dump/filecfg_dump.h"
#include "filecfg/parse/filecfg_parser.h"

static struct s_log_mqtt {
	struct {
		const char * restrict topic_root;///< MQTT log topic root for this backend, must not end with a '/'
		const char * restrict username;	///< MQTT broker username (optional)
		const char * restrict password;	///< MQTT broker password (opional)
		const char * restrict host;	///< MQTT broker host
		int port;			///< MQTT broker port (optional, defaults to 1883)
		int qos;			///< MQTT QoS value (0, 1 or 2; defaults to 0)
	} set;
	struct {
		bool online;			///< true if backend is online
	} run;
	struct mosquitto * mosq;	///< libmosquitto data
} Log_mqtt = {
	.set.topic_root = NULL,
	.set.username = NULL,
	.set.password = NULL,
	.set.host = NULL,
	.set.port = 1883,
	.set.qos = 0,
};

/**
 * Bring MQTT log backend online.
 * @return exec status
 */
static int log_mqtt_online(void)
{
	int ret;

	if (!Log_mqtt.set.host || !Log_mqtt.set.topic_root)
		return (-EMISCONFIGURED);

	mosquitto_lib_init();

	Log_mqtt.mosq = mosquitto_new(NULL, true, NULL);
	if (!Log_mqtt.mosq) {
		ret = -EOOM;
		goto fail;
	}

	ret = mosquitto_username_pw_set(Log_mqtt.mosq, Log_mqtt.set.username, Log_mqtt.set.password);
	if (ret) {
		pr_err("MQTT log username/password error: \"%s\"", mosquitto_strerror(ret));
		ret = -EGENERIC;
		goto fail;
	}

	ret = mosquitto_connect(Log_mqtt.mosq, Log_mqtt.set.host, Log_mqtt.set.port, 60);
	if (ret) {
		pr_err("MQTT log connect error: \"%s\"", mosquitto_strerror(ret));
		return (-EGENERIC);
	}


	// start the network background task
	ret = mosquitto_loop_start(Log_mqtt.mosq);
	if (ret) {
		pr_err("MQTT log loop start failed: \"%s\"", mosquitto_strerror(ret));
		ret = -EGENERIC;
		goto fail;
	}

	Log_mqtt.run.online = true;

	return (ALL_OK);

fail:
	mosquitto_disconnect(Log_mqtt.mosq);
	mosquitto_destroy(Log_mqtt.mosq);
	mosquitto_lib_cleanup();
	return (ret);
}


/**
 * Put MQTT log backend offline.
 */
static void log_mqtt_offline(void)
{
	if (!Log_mqtt.run.online)
		return;

	Log_mqtt.run.online = false;

	mosquitto_disconnect(Log_mqtt.mosq);
	mosquitto_loop_stop(Log_mqtt.mosq, true);	// force, possibly due to https://github.com/eclipse/mosquitto/issues/1555

	mosquitto_destroy(Log_mqtt.mosq);

	mosquitto_lib_cleanup();

	free((void *)Log_mqtt.set.host);
	free((void *)Log_mqtt.set.username);
	free((void *)Log_mqtt.set.password);
	free((void *)Log_mqtt.set.topic_root);
}

/**
 * Create the MQTT log database. NOP.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_mqtt_create(const char * restrict const identifier __attribute__((unused)), const struct s_log_data * const log_data __attribute__((unused)))
{
	if (!Log_mqtt.run.online)
		return (-EOFFLINE);

	return (ALL_OK);
}

/**
 * Update the MQTT log database.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_mqtt_update(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	static char message[16];	// log_value_t is int32, so 10 digits + sign + '\0'. 16 is enough
	char * topic = NULL, *p;
	size_t basesize;
	int msize, ret;
	unsigned int i;

	assert(identifier && log_data);

	if (!Log_mqtt.run.online)
		return (-EOFFLINE);

	snprintf_automalloc(topic, basesize, "%s/%s/", Log_mqtt.set.topic_root, identifier);
	if (!topic)
		return (-EOOM);

	// basesize accounts for terminating '\0'

	for (i = 0; i < log_data->nvalues; i++) {
		p = topic;	// save current in case realloc fails (poor man's reallocf() implementation)
		topic = realloc(topic, basesize + strlen(log_data->keys[i]));
		if (!topic) {
			ret = -EOOM;
			topic = p;
			goto cleanup;
		}
		strcpy(topic + (basesize - 1), log_data->keys[i]);	// append leaf topic

		msize = snprintf(message, 16, "%d", log_data->values[i]);
		assert(msize < 16);

		ret = mosquitto_publish(Log_mqtt.mosq, NULL, topic, msize, message, Log_mqtt.set.qos, false);
		if (ret) {
			dbgerr("mosquitto_publish failed: \"%s\"", mosquitto_strerror(ret));
			ret = -ESTORE;
			goto cleanup;
		}
	}

	ret = ALL_OK;

cleanup:
	free(topic);
	return (ret);
}

static const struct s_log_bendcbs log_mqtt_cbs = {
	.bkid		= LOG_BKEND_MQTT,
	.unversioned	= true,
	.separator	= '/',
	.log_online	= log_mqtt_online,
	.log_offline	= log_mqtt_offline,
	.log_create	= log_mqtt_create,
	.log_update	= log_mqtt_update,
};

void log_mqtt_hook(const struct s_log_bendcbs ** restrict const callbacks)
{
	assert(callbacks);
	*callbacks = &log_mqtt_cbs;
}

void log_mqtt_filecfg_dump(void)
{
	if (!Log_mqtt.run.online)
		return;

	filecfg_dump_nodestr("topic_root", Log_mqtt.set.topic_root);	// mandatory
	filecfg_dump_nodestr("host", Log_mqtt.set.host);	// mandatory
	filecfg_iprintf("port %d;\n", Log_mqtt.set.port);
	filecfg_iprintf("qos %d;\n", Log_mqtt.set.qos);
	if (FCD_Exhaustive || Log_mqtt.set.username)
		filecfg_dump_nodestr("username", Log_mqtt.set.username ? Log_mqtt.set.username : "");	// optional
	if (FCD_Exhaustive || Log_mqtt.set.password)
		filecfg_dump_nodestr("password", Log_mqtt.set.password ? Log_mqtt.set.password : "");	// optional
}

FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_log_mqtt, topic_root)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_log_mqtt, host)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_log_mqtt, username)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_log_mqtt, password)
FILECFG_PARSER_INT_PARSE_SET_FUNC(true, s_log_mqtt, port)
FILECFG_PARSER_INT_PARSE_SET_FUNC(true, s_log_mqtt, qos)

/**
 * Parse MQTT logging configuration.
 * @param priv unused
 * @param node a `backend "#LOG_BKEND_MQTT_NAME"` node
 * @return exec status
 */
int log_mqtt_filecfg_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"topic_root",	true,	fcp_str_s_log_mqtt_topic_root,	NULL, },
		{ NODESTR,	"host",		true,	fcp_str_s_log_mqtt_host,	NULL, },
		{ NODESTR,	"username",	false,	fcp_str_s_log_mqtt_username,	NULL, },
		{ NODESTR,	"password",	false,	fcp_str_s_log_mqtt_password,	NULL, },
		{ NODEINT,	"port",		false,	fcp_int_s_log_mqtt_port,	NULL, },
		{ NODEINT,	"qos",		false,	fcp_int_s_log_mqtt_qos,		NULL, },
	};
	int ret;

	if ((NODESTC != node->type) || strcmp(LOG_BKEND_MQTT_NAME, node->value.stringval) || (!node->children))
		return (-EINVALID);	// we only accept NODESTC node with children

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(&Log_mqtt, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		goto fail;

	// minor sanity checks
	if (!Log_mqtt.set.host || !Log_mqtt.set.topic_root) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: missing host or topic_root"), node->name, node->lineno);
		ret = -EMISCONFIGURED;
		goto fail;
	}
	else if ('/' == Log_mqtt.set.topic_root[strlen(Log_mqtt.set.topic_root)-1]) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: extraneous ending '/' in topic_root"), node->name, node->lineno);
		ret = -EMISCONFIGURED;
		goto fail;
	}

	return (ALL_OK);

fail:
	if (Log_mqtt.set.topic_root)
		free((void *)Log_mqtt.set.topic_root);
	if (Log_mqtt.set.host)
		free((void *)Log_mqtt.set.host);
	if (Log_mqtt.set.username)
		free((void *)Log_mqtt.set.username);
	if (Log_mqtt.set.password)
		free((void *)Log_mqtt.set.password);

	return (ret);
}

