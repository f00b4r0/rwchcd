//
//  hw_backends/mqtt/filecfg.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT backend file configuration implementation.
\verbatim
backend "toto" {
	type "mqtt" {
 		topic_root "my_topic";
 		host "localhost";
 		port 1883;
 		username "user";
 		password "pass";
 		temp_unit "celsius";
 	};
	temperatures {
 		temperature "test1";
 		...
 	};
	switches {
		switch "in";
		...
	};
 	relays {
 		relay "out";
 		...
 	};
 };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "filecfg.h"
#include "filecfg/parse/filecfg_parser.h"

static int temperature_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	struct s_mqtt_temperature * t;

	if (node->children)
		return(-ENOTWANTED);

	if (hw->in.temps.l >= hw->in.temps.n)
		return (-EOOM);

	if (-ENOTFOUND != mqtt_input_ibn(hw, HW_INPUT_TEMP, node->value.stringval))
		return (-EEXISTS);

	t = &hw->in.temps.all[hw->in.temps.l];

	t->name = strdup(node->value.stringval);
	if (!t->name)
		return (-EOOM);

	t->set.configured = true;
	hw->in.temps.l++;

	return (ALL_OK);
}

static int temperatures_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "temperature");

	if (!n)
		return (-EINVALID);

	if (n >= INID_MAX)
		return (-ETOOBIG);

	hw->in.temps.all = calloc(n, sizeof(hw->in.temps.all[0]));
	if (!hw->in.temps.all)
		return (-EOOM);

	hw->in.temps.n = (inid_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "temperature", temperature_parse));
}

static int switch_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	struct s_mqtt_switch * s;

	if (node->children)
		return(-ENOTWANTED);

	if (hw->in.switches.l >= hw->in.switches.n)
		return (-EOOM);

	if (-ENOTFOUND != mqtt_input_ibn(hw, HW_INPUT_SWITCH, node->value.stringval))
		return (-EEXISTS);

	s = &hw->in.switches.all[hw->in.switches.l];

	s->name = strdup(node->value.stringval);
	if (!s->name)
		return (-EOOM);

	s->set.configured = true;
	hw->in.switches.l++;

	return (ALL_OK);
}

static int switches_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "switch");

	if (!n)
		return (-EINVALID);

	if (n >= INID_MAX)
		return (-ETOOBIG);

	hw->in.switches.all = calloc(n, sizeof(hw->in.switches.all[0]));
	if (!hw->in.switches.all)
		return (-EOOM);

	hw->in.switches.n = (inid_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "switch", switch_parse));
}

static int relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	struct s_mqtt_relay * r;

	if (node->children)
		return(-ENOTWANTED);

	if (hw->out.rels.l >= hw->out.rels.n)
		return (-EOOM);

	if (-ENOTFOUND != mqtt_output_ibn(hw, HW_OUTPUT_RELAY, node->value.stringval))
		return (-EEXISTS);

	r = &hw->out.rels.all[hw->out.rels.l];

	r->name = strdup(node->value.stringval);
	if (!r->name)
		return (-EOOM);

	r->set.configured = true;
	hw->out.rels.l++;

	return (ALL_OK);
}

static int relays_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_mqtt_pdata * const hw = priv;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "relay");

	if (!n)
		return (-EINVALID);

	if (n >= OUTID_MAX)
		return (-ETOOBIG);

	hw->out.rels.all = calloc(n, sizeof(hw->out.rels.all[0]));
	if (!hw->out.rels.all)
		return (-EOOM);

	hw->out.rels.n = (outid_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "relay", relay_parse));
}


static const char * temp_unit_str[] = {
	[MQTT_TEMP_CELSIUS]	= "celsius",
	[MQTT_TEMP_KELVIN]	= "kelvin",
};

FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_mqtt_pdata, topic_root)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_mqtt_pdata, username)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_mqtt_pdata, password)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, true, s_mqtt_pdata, host)
FILECFG_PARSER_INT_PARSE_SET_FUNC(true, s_mqtt_pdata, port)
FILECFG_PARSER_ENUM_PARSE_SET_FUNC(temp_unit_str, s_mqtt_pdata, temp_unit)

static int type_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"topic_root",	true,	fcp_str_s_mqtt_pdata_topic_root,	NULL, },
		{ NODESTR,	"username",	false,	fcp_str_s_mqtt_pdata_username,		NULL, },
		{ NODESTR,	"password",	false,	fcp_str_s_mqtt_pdata_password,		NULL, },
		{ NODESTR,	"host",		true,	fcp_str_s_mqtt_pdata_host,		NULL, },
		{ NODEINT,	"port",		false,	fcp_int_s_mqtt_pdata_port,		NULL, },
		{ NODESTR,	"temp_unit",	false,	fcp_enum_s_mqtt_pdata_temp_unit,	NULL, },
	};
	struct s_mqtt_pdata * const hw = priv;
	int ret;

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// parse node list in specified order
	ret = filecfg_parser_run_parsers(hw, parsers, ARRAY_SIZE(parsers));

	// basic sanity checks
	if (hw->set.port > 65635) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: MQTT: invalid port number"), node->name, node->lineno);
		ret = -EINVALID;
	}

	if ('/' == hw->set.topic_root[strlen(hw->set.topic_root)-1]) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: MQTT: extraneous ending '/' in topic_root"), node->name, node->lineno);
		ret = -EMISCONFIGURED;
	}

	return (ret);
}

/**
 * Parse mqtt backend configuration.
 * @param node 'backend' node to process data from
 * @return exec status
 */
int mqtt_filecfg_parse(const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers mqtt_parsers[] = {
		{ NODESTC,	"type",		true,	type_parse,		NULL, },
		{ NODELST,	"temperatures",	false,	temperatures_parse,	NULL, },
		{ NODELST,	"switches",	false,	switches_parse,		NULL, },
		{ NODELST,	"relays",	false,	relays_parse,		NULL, },
	};
	struct s_mqtt_pdata * hw;
	int ret;

	if (!node)
		return (-EINVALID);

	// match children
	ret = filecfg_parser_match_nodechildren(node, mqtt_parsers, ARRAY_SIZE(mqtt_parsers));
	if (ALL_OK != ret)
		return (ret);

	if (strcmp("mqtt", mqtt_parsers[0].node->value.stringval))	// wrong type - XXX REVIEW DIRECT INDEXING
		return (-ENOTFOUND);

	// we have the right type, let's go ahead
	dbgmsg(1, 1, "MQTT: config found");

	// instantiate mqtt hw
	hw = calloc(1, sizeof(*hw));
	if (!hw)
		return (-EOOM);

	// parse node list in specified order
	ret = filecfg_parser_run_parsers(hw, mqtt_parsers, ARRAY_SIZE(mqtt_parsers));
	if (ALL_OK != ret) {
		// XXX cleanup
		filecfg_parser_pr_err(_("MQTT: config parse error"));
		return (ret);
	}

	// register hardware backend
	ret = mqtt_backend_register(hw, node->value.stringval);
	if (ret < 0) {
		// XXX cleanup
		filecfg_parser_pr_err(_("MQTT: backend registration failed for %s (%d)"), node->value.stringval, ret);
	}

	return (ret);
}
