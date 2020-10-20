//
//  filecfg/parse/backends_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Backends subsystem file configuration parsing.
 */

#include <stdlib.h>
#include <string.h>

#include "hw_backends/dummy/filecfg.h"
#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif
#ifdef HAS_MQTT
 #include "hw_backends/mqtt/filecfg.h"
#endif

#include "backends_parse.h"
#include "filecfg_parser.h"
#include "rwchcd.h"
#include "hw_backends/hw_backends.h"

typedef int (* const hw_bknd_parser_t)(const struct s_filecfg_parser_node * const);

extern struct s_hw_backends HW_backends;

static hw_bknd_parser_t HWparsers[] = {
	dummy_filecfg_parse,
#ifdef HAS_HWP1		// XXX
	hw_p1_filecfg_parse,
#endif
#ifdef HAS_MQTT
	mqtt_filecfg_parse,
#endif
};

static int hardware_backend_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	unsigned int i;
	int ret = -EGENERIC;

	for (i = 0; ret && (i < ARRAY_SIZE(HWparsers)); i++) {
		ret = HWparsers[i](node);
		if (ALL_OK == ret)
			break;
	}

	return (ret);
}

int filecfg_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_hw_backends * const b = &HW_backends;
	unsigned int n;

	n = filecfg_parser_count_siblings(node->children, "backend");

	if (!n)
		return (-EEMPTY);

	if (n >= BID_MAX)
		return (-ETOOBIG);

	b->all = calloc(n, sizeof(b->all[0]));
	if (!b->all)
		return (-EOOM);

	b->n = (bid_t)n;

	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
}



/**
 * Find a registered backend input by name.
 * This function finds the sensor named sensor_name in backend named bkend_name
 * and populates tempid with these elements.
 * @param tempid target binid_t to populate
 * @param bkend_name name of the backend to look into
 * @param sensor_name name of the sensor to look for in that backend
 * @return execution status
 */
static int hw_backends_input_fbn(const enum e_hw_input_type type, binid_t * tempid, const char * const bkend_name, const char * const input_name)
{
	bid_t bid;
	inid_t inid;
	int ret;

	// input sanitization
	if (!tempid || !bkend_name || !input_name)
		return (-EINVALID);

	// find backend
	ret = hw_backends_bid_by_name(bkend_name);
	if (ret < 0)
		return (ret);

	bid = (bid_t)ret;

	if (!HW_backends.all[bid].cb->input_ibn)
		return (-ENOTIMPLEMENTED);

	// find sensor in that backend
	ret = HW_backends.all[bid].cb->input_ibn(HW_backends.all[bid].priv, type, input_name);
	if (ret < 0)
		return (ret);

	inid = (inid_t)ret;

	// populate target
	tempid->bid = bid;
	tempid->inid = inid;

	return (ALL_OK);
}

/**
 * Find a registered backend output by name.
 * This function finds the relay named relay_name in backend named bkend_name
 * and populates relid with these elements.
 * @param relid target boutid_t to populate
 * @param bkend_name name of the backend to look into
 * @param relay_name name of the relay to look for in that backend
 * @return execution status
 */
static int hw_backends_output_fbn(const enum e_hw_output_type type, boutid_t * relid, const char * const bkend_name, const char * const output_name)
{
	bid_t bid;
	outid_t outid;
	int ret;

	// input sanitization
	if (!relid || !bkend_name || !output_name)
		return (-EINVALID);

	// find backend
	ret = hw_backends_bid_by_name(bkend_name);
	if (ret < 0)
		return (ret);

	bid = (bid_t)ret;

	if (!HW_backends.all[bid].cb->output_ibn)
		return (-ENOTIMPLEMENTED);

	// find relay in that backend
	ret = HW_backends.all[bid].cb->output_ibn(HW_backends.all[bid].priv, type, output_name);
	if (ret < 0)
		return (ret);

	outid = (outid_t)ret;

	// populate target
	relid->bid = bid;
	relid->outid = outid;

	return (ALL_OK);
}


struct s_fcp_hwbkend {
	struct {
		const char *backend;
		const char *name;
	} set;
};

FILECFG_PARSER_STR_PARSE_SET_FUNC(true, false, s_fcp_hwbkend, backend)
FILECFG_PARSER_STR_PARSE_SET_FUNC(true, false, s_fcp_hwbkend, name)

/**
 * Parse a temperature sensor configuration reference.
 * @param priv a pointer to a binid_t structure which will be populated
 * @param node the configuration node to populate from
 * @return exec status
 */
int filecfg_backends_parser_inid_parse(const enum e_hw_input_type type, void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	binid_t * restrict const binid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"backend",	true,	fcp_str_s_fcp_hwbkend_backend,	NULL, },
		{ NODESTR,	"name",		true,	fcp_str_s_fcp_hwbkend_name,	NULL, },
	};
	struct s_fcp_hwbkend p;
	int ret;

	assert(NODELST == node->type);

	dbgmsg(3, 1, "Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg(3, 1, "empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(&p, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// XXX
	ret = hw_backends_input_fbn(type, binid, p.set.backend, p.set.name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or sensor \"%s\" not found"), node->name, node->lineno, p.set.backend, p.set.name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_sensor_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

/**
 * Parse a relay configuration reference.
 * @param priv a pointer to a boutid_t structure which will be populated
 * @param node the configuration node to populate from
 * @return exec status
 */
int filecfg_backends_parser_outid_parse(const enum e_hw_output_type type, void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	boutid_t * restrict const boutid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"backend",	true,	fcp_str_s_fcp_hwbkend_backend,	NULL, },
		{ NODESTR,	"name",		true,	fcp_str_s_fcp_hwbkend_name,	NULL, },
	};
	struct s_fcp_hwbkend p;
	int ret;

	assert(NODELST == node->type);

	dbgmsg(3, 1, "Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg(3, 1, "empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = filecfg_parser_run_parsers(&p, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = hw_backends_output_fbn(type, boutid, p.set.backend, p.set.name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or relay \"%s\" not found"), node->name, node->lineno, p.set.backend, p.set.name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_relay_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}
