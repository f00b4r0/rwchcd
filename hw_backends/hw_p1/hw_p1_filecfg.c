//
//  hw_backends/hw_p1/hw_p1_filecfg.c
//  rwchcd
//
//  (C) 2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 file configuration implementation.
 */

#include <inttypes.h>
#include <assert.h>

#include "lib.h"
#include "filecfg/dump/filecfg_dump.h"
#include "filecfg/parse/filecfg_parser.h"
#include "hw_backends/hw_lib.h"
#include "hw_p1.h"
#include "hw_p1_setup.h"
#include "hw_p1_backend.h"
#include "hw_p1_filecfg.h"


static const char * const sensor_type_str[] = {
	[HW_P1_ST_NONE] = "",
	[HW_P1_ST_PT1000] = "PT1000",
	[HW_P1_ST_NI1000] = "NI1000",
};

static void config_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	assert(hw);

	filecfg_iprintf("type \"hw_p1\" {\n");
	filecfg_ilevel_inc();

	filecfg_iprintf("nsamples %" PRIdFAST8 ";\n", hw->set.nsamples);
	filecfg_iprintf("nsensors %d;\n", hw->settings.nsensors);
	filecfg_iprintf("lcdbl %d;\n", hw->settings.lcdblpct);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump a hardware sensor to config.
 * @param sensor the sensor to dump
 */
static void sensor_dump(const struct s_hw_p1_sensor * const sensor)
{
	const char * type;

	if (!sensor->set.configured)
		return;

	switch (sensor->set.type) {
		case HW_P1_ST_PT1000:
		case HW_P1_ST_NI1000:
			type = sensor_type_str[sensor->set.type];
			break;
		case HW_P1_ST_NONE:
		default:
			type = sensor_type_str[HW_P1_ST_NONE];
			break;
	}

	filecfg_iprintf("sensor \"%s\" {\n", sensor->name);
	filecfg_ilevel_inc();
	filecfg_iprintf("channel %d;\n", sensor->set.channel);
	filecfg_dump_nodestr("type", type);
	if (FCD_Exhaustive || sensor->set.offset)
		filecfg_dump_deltaK("offset", sensor->set.offset);
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

static void sensors_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	int_fast8_t id;

	assert(hw);

	if (!FCD_Exhaustive && !hw->settings.nsensors)
		return;

	filecfg_iprintf("sensors {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < hw->settings.nsensors; id++)
		sensor_dump(&hw->Sensors[id]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump a hardware relay to config.
 * @param relay the relay to dump
 */
static void relay_dump(const struct s_hw_p1_relay * const relay)
{
	if (!relay->set.configured)
		return;

	filecfg_iprintf("relay \"%s\" {\n", relay->name);
	filecfg_ilevel_inc();
	filecfg_iprintf("channel %d;\n", relay->set.channel);
	filecfg_iprintf("failstate %s;\n", relay->set.failstate ? "on" : "off");
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

static void relays_dump(const struct s_hw_p1_pdata * restrict const hw)
{
	uint_fast8_t id;

	assert(hw);

	filecfg_iprintf("relays {\n");
	filecfg_ilevel_inc();

	for (id = 0; id < ARRAY_SIZE(hw->Relays); id++)
		relay_dump(&hw->Relays[id]);

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");
}

/**
 * Dump backend configuration to file.
 * @param priv private hardware data
 * @return exec status
 */
int hw_p1_filecfg_dump(void * priv)
{
	struct s_hw_p1_pdata * const hw = priv;

	if (!hw)
		return (-EINVALID);

	config_dump(hw);
	sensors_dump(hw);
	relays_dump(hw);

	return (ALL_OK);
}

static int parse_type_nsamples(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (hw_p1_setup_setnsamples((struct s_hw_p1_pdata *)priv, node->value.intval));
}

static int parse_type_nsensors(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (hw_p1_setup_setnsensors((struct s_hw_p1_pdata *)priv, node->value.intval));
}

static int parse_type_lcdbl(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (hw_p1_setup_setbl((struct s_hw_p1_pdata *)priv, node->value.intval));
}

static int parse_type(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "nsamples", true, parse_type_nsamples, NULL, },
		{ NODEINT, "nsensors", true, parse_type_nsensors, NULL, },
		{ NODEINT, "lcdbl", false, parse_type_lcdbl, NULL, },
	};
	int ret = ALL_OK;

	assert(priv && node);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers));

	return (ret);
}

/**
 * Parse a hardware sensor from config.
 * @param priv an allocated sensor structure which will be populated according to parsed configuration
 * @param node the configuration node
 * @return exec status
 */
static int sensor_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "channel", true, NULL, NULL, },
		{ NODESTR, "type", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "offset", false, NULL, NULL, },
	};
	struct s_hw_p1_sensor sensor;
	const char * sensor_stype;
	float sensor_offset;
	unsigned int i;
	int ret;

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	sensor.set.channel = parsers[0].node->value.intval;		// XXX REVIEW DIRECT INDEXING
	sensor_stype = parsers[1].node->value.stringval;
	if (parsers[2].node)
		sensor_offset = (NODEFLT == parsers[2].node->type) ? parsers[2].node->value.floatval : (float)parsers[2].node->value.intval;
	else
		sensor_offset = 0;

	sensor.set.offset = deltaK_to_temp(sensor_offset);

	// match stype
	for (i = 0; i < ARRAY_SIZE(sensor_type_str); i++) {
		if (!strcmp(sensor_type_str[i], sensor_stype)) {
			sensor.set.type = i;
			break;
		}
	}

	sensor.name = node->value.stringval;	// will be copied in sensor_configure()

	ret = hw_p1_setup_sensor_configure(priv, &sensor);
	switch (ret) {
		case -EINVALID:
			filecfg_parser_pr_err(_("Line %d: invalid sensor type or id"), node->lineno);
			break;
		case -EEXISTS:
			filecfg_parser_pr_err(_("Line %d: a sensor with the same name or id is already configured"), node->lineno);
			break;
		case -EUNKNOWN:
			filecfg_parser_pr_err(_("Line %d: unknown sensor type \"%s\""), parsers[1].node->lineno, sensor_stype);
			break;
		default:
			break;
	}

	return (ret);
}

static int sensors_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "sensor", sensor_parse));
}

static int relay_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "channel", true, NULL, NULL, },
		{ NODEBOL, "failstate", true, NULL, NULL, },
	};
	struct s_hw_p1_relay relay;
	int ret;

	// match children
	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// return if invalid config

	relay.set.channel = parsers[0].node->value.intval;		// XXX REVIEW DIRECT INDEXING
	relay.set.failstate = parsers[1].node->value.boolval;
	relay.name = node->value.stringval;	// will be copied in relay_request()

	ret = hw_p1_setup_relay_request(priv, &relay);
	switch (ret) {
		case -EINVALID:
			filecfg_parser_pr_err(_("Line %d: invalid relay id"), node->lineno);
			break;
		case -EEXISTS:
			filecfg_parser_pr_err(_("Line %d: a relay with the same name or id is already configured"), node->lineno);
			break;
		default:
			break;
	}

	return (ret);
}

static int relays_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "relay", relay_parse));
}

/**
 * Parse backend configuration.
 * @param node 'backend' node to process data from
 * @return exec status
 */
int hw_p1_filecfg_parse(const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers hw_p1_parsers[] = {
		{ NODESTR, "type", true, parse_type, NULL, },
		{ NODELST, "sensors", false, sensors_parse, NULL, },
		{ NODELST, "relays", false, relays_parse, NULL, },
	};
	void * hw;
	int ret;

	if (!node)
		return (-EINVALID);

	if ((NODESTR != node->type) || strcmp("backend", node->name) || (!node->children))
		return (-EINVALID);	// we only accept NODESTR backend node with children

	// match children
	ret = filecfg_parser_match_nodechildren(node, hw_p1_parsers, ARRAY_SIZE(hw_p1_parsers));
	if (ALL_OK != ret)
		return (ret);

	if (strcmp("hw_p1", hw_p1_parsers[0].node->value.stringval))	// wrong type - XXX REVIEW DIRECT INDEXING
		return (-ENOTFOUND);

	// we have the right type, let's go ahead
	dbgmsg(1, 1, "HW P1 config found");

	// instantiate hardware proto 1
	hw = hw_p1_setup_new();
	if (!hw)
		return (-EOOM);

	// parse node list in specified order
	ret = filecfg_parser_run_parsers(hw, hw_p1_parsers, ARRAY_SIZE(hw_p1_parsers));
	if (ALL_OK != ret) {
		hw_p1_setup_del(hw);
		filecfg_parser_pr_err(_("HWP1 config parse error"));
		return (ret);
	}

	// register hardware backend
	ret = hw_p1_backend_register(hw, node->value.stringval);
	if (ret < 0) {
		hw_p1_setup_del(hw);
		filecfg_parser_pr_err(_("HWP1: backend registration failed for %s (%d)"), node->value.stringval, ret);
	}

	return (ret);
}
