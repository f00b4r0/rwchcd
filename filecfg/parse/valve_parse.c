//
//  filecfg/parse/valve_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump subsystem file configuration parsing.
 */

#include <string.h>

#include "inputs_parse.h"
#include "outputs_parse.h"
#include "valve_parse.h"
#include "plant/valve.h"
#include "plant/plant.h"
#include "filecfg_parser.h"


FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_valve_sapprox_priv, sample_intvl)
FILECFG_PARSER_INTPOSMAX_PARSE_SET_FUNC(1000, s_valve_sapprox_priv, amount)

static int valve_algo_sapprox_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"sample_intvl",	true,	fcp_tk_s_valve_sapprox_priv_sample_intvl,	NULL, },
		{ NODEINT,		"amount",	true,	fcp_int_s_valve_sapprox_priv_amount,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	struct s_valve_sapprox_priv sapriv = { 0 };
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&sapriv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = valve_make_sapprox(valve, sapriv.set.amount, sapriv.set.sample_intvl);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		default:	// should never happen
			dbgerr("valve_make_sapprox() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_valve_pi_priv, sample_intvl)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_valve_pi_priv, Tu)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_valve_pi_priv, Td)
FILECFG_PARSER_INTPOSMAX_PARSE_SET_FUNC(UINT_FAST8_MAX, s_valve_pi_priv, tune_f)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(true, true, s_valve_pi_priv, Ksmax)

static int valve_algo_PI_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"sample_intvl",	true,	fcp_tk_s_valve_pi_priv_sample_intvl,	NULL, },
		{ NODEINT|NODEDUR,	"Tu",		true,	fcp_tk_s_valve_pi_priv_Tu,		NULL, },
		{ NODEINT|NODEDUR,	"Td",		true,	fcp_tk_s_valve_pi_priv_Td,		NULL, },
		{ NODEINT,		"tune_f",	true,	fcp_int_s_valve_pi_priv_tune_f,		NULL, },
		{ NODEFLT|NODEINT,	"Ksmax",	true,	fcp_temp_s_valve_pi_priv_Ksmax,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	struct s_valve_pi_priv pipriv = { 0 };
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(&pipriv, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	ret = valve_make_pi(valve, pipriv.set.sample_intvl, pipriv.set.Td, pipriv.set.Tu, pipriv.set.Ksmax, pipriv.set.tune_f);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		case -EMISCONFIGURED:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: incorrect values for sample_intvl '%d' vs Tu '%d'"), node->name, node->lineno, parsers[0].node->value.intval, parsers[1].node->value.intval);
			break;
		default:	// should never happen
			dbgerr("valve_make_sapprox() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int fcp_tid_valve_tmix_tid_hot(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_inputs_parse_helper_tid(&valve->set.tset.tmix.tid_hot, node));
}

static int fcp_tid_valve_tmix_tid_cold(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_inputs_parse_helper_tid(&valve->set.tset.tmix.tid_cold, node));
}

static int fcp_tid_valve_tmix_tid_out(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_inputs_parse_helper_tid(&valve->set.tset.tmix.tid_out, node));
}

static int fcp_temp_valve_tmix_tdeadzone(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	int ret; temp_t temp = 0;
	ret = filecfg_parser_get_node_temp(true, true, node, &temp);
	valve->set.tset.tmix.tdeadzone = temp;	/* Note: always set */
	return (ret);
}

static int fcp_valve_tmix_algo(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str = node->value.stringval;
	int ret;

	if	(!strcmp(str, "PI"))
		ret = valve_algo_PI_parser(valve, node);
	else if (!strcmp(str, "sapprox"))
		ret = valve_algo_sapprox_parser(valve, node);
	else if (!strcmp(str, "bangbang"))
		ret = valve_make_bangbang(valve);
	else
		ret = -EINVALID;

	return (ret);
}

static int valve_tmix_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT,	"tdeadzone",	false,	fcp_temp_valve_tmix_tdeadzone,	NULL, },
		{ NODESTR,		"tid_hot",	false,	fcp_tid_valve_tmix_tid_hot,	NULL, },
		{ NODESTR,		"tid_cold",	false,	fcp_tid_valve_tmix_tid_cold,	NULL, },
		{ NODESTR,		"tid_out",	true,	fcp_tid_valve_tmix_tid_out,	NULL, },
		{ NODESTR|NODESTC,	"algo",		true,	fcp_valve_tmix_algo,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	valve->set.type = VA_TYPE_MIX;	// needed by valve_make_* algos

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));

	return (ret);
}

static int valve_tisol_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;

	valve->set.type = VA_TYPE_ISOL;

	return (ALL_OK);
}

FILECFG_PARSER_INTPOSMAX_PARSE_NEST_FUNC(1000, s_valve, set.mset.m3way., deadband)

static int fcp_rid_valve_m3way_rid_open(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_outputs_parse_helper_rid(&valve->set.mset.m3way.rid_open, node));
}

static int fcp_rid_valve_m3way_rid_close(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_outputs_parse_helper_rid(&valve->set.mset.m3way.rid_close, node));
}

static int valve_m3way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT,	"deadband",	false,	fcp_int_s_valve_deadband,	NULL, },
		{ NODESTR,	"rid_open",	true,	fcp_rid_valve_m3way_rid_open,	NULL, },
		{ NODESTR,	"rid_close",	true,	fcp_rid_valve_m3way_rid_close,	NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.motor = VA_M_3WAY;

	return (ALL_OK);
}

static int fcp_rid_valve_m2way_rid_trigger(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	return (filecfg_outputs_parse_helper_rid(&valve->set.mset.m2way.rid_trigger, node));
}

static int fcp_bool_valve_m2way_trigger_opens(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	valve->set.mset.m2way.trigger_opens = node->value.boolval;
	return (ALL_OK);
}

static int valve_m2way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR,	"rid_trigger",		true,	fcp_rid_valve_m2way_rid_trigger,	NULL, },
		{ NODEBOL,	"trigger_opens",	true,	fcp_bool_valve_m2way_trigger_opens,	NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->set.motor = VA_M_2WAY;

	return (ALL_OK);
}

FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_valve, ete_time)

static int fcp_valve_type(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str = node->value.stringval;
	int ret;

	if	(!strcmp(str, "mix"))
		ret = valve_tmix_parser(valve, node);
	else if (!strcmp(str, "isol"))
		ret = valve_tisol_parser(valve, node);
	else
		ret = -EINVALID;

	return (ret);
}

static int fcp_valve_motor(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_valve * restrict const valve = priv;
	const char * str = node->value.stringval;
	int ret;

	if	(!strcmp(str, "3way"))
		ret = valve_m3way_parser(valve, node);
	else if (!strcmp(str, "2way"))
		ret = valve_m2way_parser(valve, node);
	else
		ret = -EINVALID;

	return (ret);
}

int filecfg_valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT|NODEDUR,	"ete_time",	true,	fcp_tk_s_valve_ete_time,	NULL, },
		{ NODESTR|NODESTC,	"type",		true,	fcp_valve_type,			NULL, },
		{ NODESTC,		"motor",	true,	fcp_valve_motor,		NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret;

	// we receive a 'valve' node with a valid string attribute which is the valve name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(valve, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	valve->name = strdup(node->value.stringval);
	if (!valve->name)
		return (-EOOM);

	valve->set.configured = true;

	return (ALL_OK);
}
