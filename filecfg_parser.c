//
//  filecfg_parser.c
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File config parser implementation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hw_backends.h"
#include "config.h"
#include "lib.h"
#include "filecfg_parser.h"

#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif

#include "models.h"

#include "plant.h"
#include "pump.h"
#include "valve.h"
#include "dhwt.h"
#include "hcircuit.h"

#ifndef ARRAY_SIZE
 #define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))
#endif

/**
 * Create a new configuration node.
 * This routine is used by the Bison parser.
 * @param lineno the line number for the new node
 * @param type the type of the new node
 * @param name the name of the new node
 * @param value the value of the new node
 * @param children the children of the new node (if any)
 * @return a properly populated node structure
 * @note the function will forcefully exit if OOM
 */
struct s_filecfg_parser_node * filecfg_parser_new_node(int lineno, int type, char *name, union u_filecfg_parser_nodeval value, struct s_filecfg_parser_nodelist *children)
{
	struct s_filecfg_parser_node * node = calloc(1, sizeof(*node));

	if (!node) {
		perror(NULL);
		exit(-1);
	}

	printf("new_node: %d, %d, %s, ", lineno, type, name);
	switch (type) {
		case NODEINT:
		case NODEBOL:
			printf("%d\n", value.intval);
			break;
		case NODEFLT:
			printf("%f\n", value.floatval);
			break;
		case NODESTR:
			printf("%s\n", value.stringval);
			break;
		case NODELST:
			printf("{list}\n");
			break;
	}

	node->lineno = lineno;
	node->type = type;
	node->name = name;
	node->value = value;
	node->children = children;

	return (node);
}

/**
 * Insert a configuration node into a node list.
 * This routine is used by the Bison parser.
 * @param next a pointer to the next list member
 * @param node a pointer to the node to insert
 * @return the newly created list member
 * @note the function will forcefully exit if OOM
 */
struct s_filecfg_parser_nodelist * filecfg_parser_new_nodelistelmt(struct s_filecfg_parser_nodelist *next, struct s_filecfg_parser_node *node)
{
	struct s_filecfg_parser_nodelist * listelmt = calloc(1, sizeof(*listelmt));

	if (!listelmt) {
		perror(NULL);
		exit(-1);
	}

	listelmt->next = next;
	listelmt->node = node;

	return (listelmt);
}

static int hardware_backend_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	int ret;
	ret = hw_p1_filecfg_parse(node);
	if (ALL_OK == ret)	// XXX HACK
		dbgmsg("HW P1 found!");

	return (ret);
}

static int tid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	tempid_t * restrict const tempid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "backend", true, NULL, false, NULL, },
		{ NODESTR, "name", true, NULL, false, NULL, },
	};
	const char * backend, * name;
	int ret;

	dbgmsg("Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg("empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	return (hw_backends_sensor_fbn(tempid, backend, name));
}

static int rid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	relid_t * restrict const relid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "backend", true, NULL, false, NULL, },
		{ NODESTR, "name", true, NULL, false, NULL, },
	};
	const char * backend, * name;
	int ret;

	dbgmsg("Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg("empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	return (hw_backends_relay_fbn(relid, backend, name));
}

static int dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT, "t_comfort", false, NULL, false, NULL, },		// 0
		{ NODEFLT, "t_eco", false, NULL, false, NULL, },
		{ NODEFLT, "t_frostfree", false, NULL, false, NULL, },		// 2
		{ NODEFLT, "t_legionella", false, NULL, false, NULL, },
		{ NODEFLT, "limit_tmin", false, NULL, false, NULL, },		// 4
		{ NODEFLT, "limit_tmax", false, NULL, false, NULL, },
		{ NODEFLT, "limit_wintmax", false, NULL, false, NULL, },	// 6
		{ NODEFLT, "hysteresis", false, NULL, false, NULL, },
		{ NODEFLT, "temp_inoffset", false, NULL, false, NULL, },	// 8
		{ NODEINT, "limit_chargetime", false, NULL, false, NULL, },
	};
	struct s_dhwt_params * restrict const dhwt_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		fv = currnode->value.floatval;
		delta = deltaK_to_temp(fv);
		celsius = celsius_to_temp(fv);

		switch (i) {
			case 0:
				dhwt_params->t_comfort = celsius;
				break;
			case 1:
				dhwt_params->t_eco = celsius;
				break;
			case 2:
				dhwt_params->t_frostfree = celsius;
				break;
			case 3:
				dhwt_params->t_legionella = celsius;
				break;
			case 4:
				dhwt_params->limit_tmin = celsius;
				break;
			case 5:
				dhwt_params->limit_tmax = celsius;
				break;
			case 6:
				dhwt_params->limit_wintmax = celsius;
				break;
			case 7:
				if (fv < 0)
					goto invaliddata;
				else
					dhwt_params->hysteresis = delta;
				break;
			case 8:
				dhwt_params->temp_inoffset = delta;
				break;
			case 9:
				if (currnode->value.intval < 0)
					goto invaliddata;
				else
					dhwt_params->limit_chargetime = currnode->value.intval;
				break;
			default:
				break;	// never happen
		}
	}

	return (ALL_OK);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT, "t_comfort", false, NULL, false, NULL, },		// 0
		{ NODEFLT, "t_eco", false, NULL, false, NULL, },
		{ NODEFLT, "t_frostfree", false, NULL, false, NULL, },		// 2
		{ NODEFLT, "t_offset", false, NULL, false, NULL, },
		{ NODEFLT, "outhoff_comfort", false, NULL, false, NULL, },	// 4
		{ NODEFLT, "outhoff_eco", false, NULL, false, NULL, },
		{ NODEFLT, "outhoff_frostfree", false, NULL, false, NULL, },	// 6
		{ NODEFLT, "outhoff_hysteresis", false, NULL, false, NULL, },
		{ NODEFLT, "limit_wtmin", false, NULL, false, NULL, },		// 8
		{ NODEFLT, "limit_wtmax", false, NULL, false, NULL, },
		{ NODEFLT, "temp_inoffset", false, NULL, false, NULL, },	// 10
	};
	struct s_hcircuit_params * restrict const hcircuit_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		fv = currnode->value.floatval;
		delta = deltaK_to_temp(fv);
		celsius = celsius_to_temp(fv);

		switch (i) {
			case 0:
				hcircuit_params->t_comfort = celsius;
				break;
			case 1:
				hcircuit_params->t_eco = celsius;
				break;
			case 2:
				hcircuit_params->t_frostfree = celsius;
				break;
			case 3:
				hcircuit_params->t_offset = delta;
				break;
			case 4:
				hcircuit_params->outhoff_comfort = celsius;
				break;
			case 5:
				hcircuit_params->outhoff_eco = celsius;
				break;
			case 6:
				hcircuit_params->outhoff_frostfree = celsius;
				break;
			case 7:
				if (fv < 0)
					goto invaliddata;
				else
					hcircuit_params->outhoff_hysteresis = delta;
				break;
			case 8:
				hcircuit_params->limit_wtmin = celsius;
				break;
			case 9:
				hcircuit_params->limit_wtmax = celsius;
				break;
			case 10:
				hcircuit_params->temp_inoffset = delta;
				break;
			default:
				break;	// never happen
		}
	}

	return (ALL_OK);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "summer_maintenance", false, NULL, false, NULL, },	// 0
		{ NODEBOL, "logging", false, NULL, false, NULL, },
		{ NODEFLT, "limit_tsummer", false, NULL, false, NULL, },	// 2
		{ NODEFLT, "limit_tfrost", false, NULL, false, NULL, },
		{ NODEINT, "sleeping_delay", false, NULL, false, NULL, },	// 4
		{ NODELST, "def_hcircuit", false, NULL, false, NULL, },
		{ NODELST, "def_dhwt", false, NULL, false, NULL, },		// 6
	};
	struct s_config * restrict config;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	int ret;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	config = config_new();
	if (!config)
		return (-EOOM);

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				config->summer_maintenance = currnode->value.boolval;
				break;
			case 1:
				config->logging = currnode->value.boolval;
				break;
			case 2:
				ret = config_set_tsummer(config, celsius_to_temp(currnode->value.floatval));
				break;
			case 3:
				ret = config_set_tfrost(config, celsius_to_temp(currnode->value.floatval));
				break;
			case 4:
				if (currnode->value.intval < 0)
					goto invaliddata;
				else
					config->sleeping_delay = currnode->value.intval;
			case 5:
				if (ALL_OK != hcircuit_params_parse(&config->def_hcircuit, currnode))
					goto invaliddata;
				break;
			case 6:
				if (ALL_OK != dhwt_params_parse(&config->def_dhwt, currnode))
					goto invaliddata;
				break;
			default:
				break;
		}
	}

	config->configured = true;

	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
	return (ALL_OK);

	// we choose to interrupt parsing if an error occurs in this function, but let the subparsers run to the end
invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int bmodel_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "logging", false, NULL, false, NULL, },
		{ NODEINT, "tau", true, NULL, false, NULL, },
		{ NODELST, "tid_outdoor", true, NULL, false, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_bmodel * bmodel;
	const char * bmdlname = node->value.stringval;
	int iv, ret;

	// we receive a 'bmodel' node with a valid string attribute which is the bmodel name

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	bmodel = models_new_bmodel(bmdlname);
	if (!bmodel)
		return (-EOOM);

	currnode = parsers[0].node;
	if (currnode)
		bmodel->set.logging = currnode->value.boolval;

	currnode = parsers[1].node;
	iv = currnode->value.intval;
	if (iv < 0) {
		dbgerr("Invalid negative value for \"%s\" closing at line %d", currnode->name, currnode->lineno);
		return (-EINVALID);
	}

	currnode = parsers[2].node;
	ret = tid_parse(&bmodel->set.tid_outdoor, currnode);
	if (ALL_OK != ret) {
		dbgerr("tid_parse failed");
		return (ret);
	}

	bmodel->set.configured = true;

	dbgmsg("matched \"%s\"", bmdlname);

	return (ret);
}

#define filecfg_for_node_filter_typename_continue(NODE, TYPE, NAME)						\
	({													\
	if (TYPE != NODE->type) {										\
		dbgerr("Ignoring node \"%s\" with invalid type closing at line %d", NODE->name, NODE->lineno);	\
		continue;											\
	}													\
	if (strcmp(NAME, NODE->name)) {										\
		dbgerr("Ignoring unknown node \"%s\" closing at line %d", NODE->name, NODE->lineno);		\
		continue;											\
	}													\
	})

int filecfg_parser_parse_namedsiblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist, const char * nname, const parser_t parser)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	const char * sname;
	int ret = -EEMPTY;	// immediate return if nodelist is empty

	for (nlist = nodelist; nlist; nlist = nlist->next) {
		node = nlist->node;
		filecfg_for_node_filter_typename_continue(node, NODESTR, nname);

		sname = node->value.stringval;

		if (strlen(sname) < 1) {
			dbgerr("Ignoring \"%s\" with empty name closing at line %d", node->name, node->lineno);
			continue;
		}

		dbgmsg("Trying %s node \"%s\"", node->name, sname);

		// test parser
		ret = parser(priv, node);
		if (ALL_OK == ret)
			dbgmsg("found!");
		else
			break;	// stop processing at first fault
	}

	return (ret);
}

static int models_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "bmodel", bmodel_parse));
}

static int pump_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "cooldown_time", false, NULL, false, NULL, },
		{ NODELST, "rid_pump", true, NULL, false, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_pump * pump;
	int ret = ALL_OK;

	// we receive a 'pump' node with a valid string attribute which is the pump name

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	// create the pump
	pump = plant_new_pump(plant, node->value.stringval);
	if (!pump) {
		dbgerr("pump creation failed");
		return (-EOOM);
	}

	currnode = parsers[0].node;
	if (currnode) {
		if (currnode->value.intval < 0) {
			ret = -EINVALID;
			goto invaliddata;
		}
		else
			pump->set.cooldown_time = currnode->value.intval;
	}

	currnode = parsers[1].node;
	ret = rid_parse(&pump->set.rid_pump, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	pump->set.configured = true;

	return (ret);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (ret);
}

static int pumps_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "pump", pump_parse));
}

static int valve_algo_sapprox_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sample_intvl", true, NULL, false, NULL, },
		{ NODEINT, "amount", true, NULL, false, NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret, sample_intvl, amount;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	sample_intvl = parsers[0].node->value.intval;
	amount = parsers[1].node->value.intval;

	return (valve_make_sapprox(valve, amount, sample_intvl));
}

static int valve_algo_PI_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sample_intvl", true, NULL, false, NULL, },
		{ NODEINT, "Tu", true, NULL, false, NULL, },
		{ NODEINT, "Td", true, NULL, false, NULL, },
		{ NODEINT, "tune_f", true, NULL, false, NULL, },
		{ NODEFLT, "Ksmax", true, NULL, false, NULL, },
	};
	struct s_valve * restrict const valve = priv;
	int ret, sample_intvl, Tu, Td, tune_f;
	float Ksmax;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	sample_intvl = parsers[0].node->value.intval;
	Tu = parsers[1].node->value.intval;
	Td = parsers[2].node->value.intval;
	tune_f = parsers[3].node->value.intval;
	Ksmax = parsers[4].node->value.floatval;

	return (valve_make_pi(valve, sample_intvl, Td, Tu, Ksmax, tune_f));
}

static int valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "deadband", false, NULL, false, NULL, },	// 0
		{ NODEINT, "ete_time", true, NULL, false, NULL, },
		{ NODEFLT, "tdeadzone", false, NULL, false, NULL, },	// 2
		{ NODELST, "tid_hot", false, NULL, false, NULL, },
		{ NODELST, "tid_cold", false, NULL, false, NULL, },	// 4
		{ NODELST, "tid_out", true, NULL, false, NULL, },
		{ NODELST, "rid_hot", true, NULL, false, NULL, },	// 6
		{ NODELST, "rid_cold", true, NULL, false, NULL, },
		{ NODESTR, "algo", true, NULL, false, NULL, },		// 8
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_valve * valve;
	const char * n;
	float fv;
	int iv, ret;
	unsigned int i;

	// we receive a 'valve' node with a valid string attribute which is the valve name

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	// create the valve
	valve = plant_new_valve(plant, node->value.stringval);
	if (!valve) {
		dbgerr("valve creation failed");
		return (-EOOM);
	}

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
			case 1:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				if (0 == i)
					valve->set.deadband = iv;
				else	// i == 1
					valve->set.ete_time = iv;
				break;
			case 2:
				fv = currnode->value.floatval;
				if (fv < 0)
					goto invaliddata;
				else
					valve->set.tdeadzone = deltaK_to_temp(fv);
				break;
			case 3:
				if (ALL_OK != tid_parse(&valve->set.tid_hot, currnode))
					goto invaliddata;
				break;
			case 4:
				if (ALL_OK != tid_parse(&valve->set.tid_cold, currnode))
					goto invaliddata;
				break;
			case 5:
				if (ALL_OK != tid_parse(&valve->set.tid_out, currnode))
					goto invaliddata;
				break;
			case 6:
				if (ALL_OK != rid_parse(&valve->set.rid_hot, currnode))
					goto invaliddata;
				break;
			case 7:
				if (ALL_OK != rid_parse(&valve->set.rid_cold, currnode))
					goto invaliddata;
				break;
			case 8:
				n = currnode->value.stringval;
				if (!strcmp("PI", n))
					ret = valve_algo_PI_parser(valve, currnode);
				else if (!strcmp("sapprox", n))
					ret = valve_algo_sapprox_parser(valve, currnode);
				else if (!strcmp("bangbang", n))
					ret = valve_make_bangbang(valve);
				else {
					dbgerr("Unknown algo \"%s\" closing at line %d", n, currnode->lineno);
					return (-EUNKNOWN);
				}
				if (ALL_OK == ret)
					dbgmsg("parsed algo \"%s\"", n);
				break;
			default:
				break;	// should never happen
		}
	}

	if (ALL_OK == ret)
		valve->set.configured = true;

	return (ret);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int valves_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "valve", valve_parse));
}

static int runmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	enum e_runmode * restrict const runmode = priv;
	const char * n;

	n = node->value.stringval;

	if (!strcmp("off", n))
		*runmode = RM_OFF;
	else if (!strcmp("auto", n))
		*runmode = RM_AUTO;
	else if (!strcmp("comfort", n))
		*runmode = RM_COMFORT;
	else if (!strcmp("eco", n))
		*runmode = RM_ECO;
	else if (!strcmp("frostfree", n))
		*runmode = RM_FROSTFREE;
	else if (!strcmp("test", n))
		*runmode = RM_TEST;
	else if (!strcmp("dhwonly", n))
		*runmode = RM_DHWONLY;
	else {
		*runmode = RM_UNKNOWN;
		dbgerr("Unknown runmode \"%s\" at line %d", n, node->lineno);
		return (-EINVALID);
	}

	return (ALL_OK);
}

static int dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "electric_failover", false, NULL, false, NULL, },	// 0
		{ NODEBOL, "anti_legionella", false, NULL, false, NULL, },
		{ NODEBOL, "legionella_recycle", false, NULL, false, NULL, },	// 2
		{ NODEINT, "prio", false, NULL, false, NULL, },
		{ NODESTR, "runmode", true, NULL, false, NULL, },		// 4
		{ NODESTR, "dhwt_cprio", false, NULL, false, NULL, },
		{ NODESTR, "force_mode", false, NULL, false, NULL, },		// 6
		{ NODELST, "tid_bottom", false, NULL, false, NULL, },
		{ NODELST, "tid_top", false, NULL, false, NULL, },		// 8
		{ NODELST, "tid_win", false, NULL, false, NULL, },
		{ NODELST, "tid_wout", false, NULL, false, NULL, },		// 10
		{ NODELST, "rid_selfheater", false, NULL, false, NULL, },
		{ NODELST, "params", false, NULL, false, NULL, },		// 12
		{ NODESTR, "pump_feed", false, NULL, false, NULL, },
		{ NODESTR, "pump_recycle", false, NULL, false, NULL, },		// 14
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_pump * pump;
	struct s_plant * restrict const plant = priv;
	struct s_dhw_tank * dhwt;
	const char * n;
	int iv, ret;
	unsigned int i;

	// we receive a 'dhwt' node with a valid string attribute which is the dhwt name

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	// create the dhwt
	dhwt = plant_new_dhwt(plant, node->value.stringval);
	if (!dhwt) {
		dbgerr("dhwt creation failed");
		return (-EOOM);
	}

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				dhwt->set.electric_failover = currnode->value.boolval;
				break;
			case 1:
				dhwt->set.anti_legionella = currnode->value.boolval;
				break;
			case 2:
				dhwt->set.legionella_recycle = currnode->value.boolval;
				break;

			case 3:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				dhwt->set.prio = currnode->value.intval;
				break;
			case 4:
				ret = runmode_parse(&dhwt->set.runmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 5:
				n = currnode->value.stringval;
				if (!strcmp("paralmax", n))
					dhwt->set.dhwt_cprio = DHWTP_PARALMAX;
				else if (!strcmp("paraldhw", n))
					dhwt->set.dhwt_cprio = DHWTP_PARALDHW;
				else if (!strcmp("slidmax", n))
					dhwt->set.dhwt_cprio = DHWTP_SLIDMAX;
				else if (!strcmp("sliddhw", n))
					dhwt->set.dhwt_cprio = DHWTP_SLIDDHW;
				else if (!strcmp("absolute", n))
					dhwt->set.dhwt_cprio = DHWTP_ABSOLUTE;
				else {
					dbgerr("Unknown DHW priority \"%s\" at line %d", n, currnode->lineno);
					return (-EINVALID);
				}
				break;
			case 6:
				n = currnode->value.stringval;
				if (!strcmp("never", n))
					dhwt->set.force_mode = DHWTF_NEVER;
				else if (!strcmp("first", n))
					dhwt->set.force_mode = DHWTF_FIRST;
				else if (!strcmp("always", n))
					dhwt->set.force_mode = DHWTF_ALWAYS;
				else {
					dbgerr("Unknown DHW force mode \"%s\" at line %d", n, currnode->lineno);
					return (-EINVALID);
				}
				break;
			case 7:
				if (ALL_OK != tid_parse(&dhwt->set.tid_bottom, currnode))
					goto invaliddata;
				break;
			case 8:
				if (ALL_OK != tid_parse(&dhwt->set.tid_top, currnode))
					goto invaliddata;
				break;
			case 9:
				if (ALL_OK != tid_parse(&dhwt->set.tid_win, currnode))
					goto invaliddata;
				break;
			case 10:
				if (ALL_OK != tid_parse(&dhwt->set.tid_wout, currnode))
					goto invaliddata;
				break;
			case 11:
				if (ALL_OK != rid_parse(&dhwt->set.rid_selfheater, currnode))
					goto invaliddata;
				break;
			case 12:
				if (ALL_OK != dhwt_params_parse(&dhwt->set.params, currnode))
					goto invaliddata;
				break;
			case 13:
			case 14:
				n = currnode->value.stringval;
				if (strlen(n) < 1)
					break;	// nothing to do

				pump = plant_fbn_pump(plant, n);
				if (!pump)
					goto invaliddata;	// pump not found

				dbgmsg("%s: \"%s\" found", currnode->name, n);
				if (13 == i)
					dhwt->pump_feed = pump;
				else	// i == 14
					dhwt->pump_recycle = pump;
				break;
			default:
				break;	// should never happen
		}
	}

	if (ALL_OK == ret)
		dhwt->set.configured = true;

	return (ret);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "dhwt", dhwt_parse));
}

static int hcircuit_tlaw_bilinear_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT, "tout1", true, NULL, false, NULL, },
		{ NODEFLT, "twater1", true, NULL, false, NULL, },
		{ NODEFLT, "tout2", true, NULL, false, NULL, },
		{ NODEFLT, "twater2", true, NULL, false, NULL, },
		{ NODEINT, "nH100", false, NULL, false, NULL, },
		// these shouldn't be user-configurable
/*		{ NODEFLT, "toutinfl", false, NULL, false, NULL, },
		{ NODEFLT, "twaterinfl", false, NULL, false, NULL, },
		{ NODEFLT, "offset", false, NULL, false, NULL, },
		{ NODEFLT, "slope", false, NULL, false, NULL, },*/
	};
	struct s_hcircuit * restrict const hcircuit = priv;
	temp_t tout1, twater1, tout2, twater2;
	int ret, nH100;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	tout1 = celsius_to_temp(parsers[0].node->value.floatval);
	twater1 = celsius_to_temp(parsers[1].node->value.floatval);
	tout2 = celsius_to_temp(parsers[2].node->value.floatval);
	twater2 = celsius_to_temp(parsers[3].node->value.floatval);
	nH100 = parsers[4].node->value.intval;

	return (circuit_make_bilinear(hcircuit, tout1, twater1, tout2, twater2, nH100));
}

static int hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "fast_cooldown", false, NULL, false, NULL, },	// 0
		{ NODEBOL, "logging", false, NULL, false, NULL, },
		{ NODESTR, "runmode", true, NULL, false, NULL, },		// 2
		{ NODEINT, "ambient_factor", false, NULL, false, NULL, },
		{ NODEFLT, "wtemp_rorh", false, NULL, false, NULL, },		// 4
		{ NODEINT, "am_tambient_tK", false, NULL, false, NULL, },
		{ NODEFLT, "tambient_boostdelta", false, NULL, false, NULL, },	// 6
		{ NODEINT, "boost_maxtime", false, NULL, false, NULL, },
		{ NODELST, "tid_outgoing", true, NULL, false, NULL, },		// 8
		{ NODELST, "tid_return", false, NULL, false, NULL, },
		{ NODELST, "tid_ambient", false, NULL, false, NULL, },		// 10
		{ NODELST, "params", false, NULL, false, NULL, },
		{ NODESTR, "tlaw", true, NULL, false, NULL, },			// 12
		{ NODESTR, "valve_mix", false, NULL, false, NULL, },
		{ NODESTR, "pump_feed", false, NULL, false, NULL, },		// 14
		{ NODESTR, "bmodel", false, NULL, false, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_hcircuit * hcircuit;
	const char * n;
	float fv;
	int iv, ret;
	unsigned int i;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret) {
		dbgerr("Incomplete \"%s\" node configuration closing at line %d", node->name, node->lineno);
		return (ret);	// break if invalid config
	}

	// create the hcircuit
	hcircuit = plant_new_circuit(plant, node->value.stringval);
	if (!hcircuit) {
		dbgerr("hcircuit creation failed");
		return (-EOOM);
	}

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				hcircuit->set.fast_cooldown = currnode->value.boolval;
				break;
			case 1:
				hcircuit->set.logging = currnode->value.boolval;
				break;
			case 2:
				ret = runmode_parse(&hcircuit->set.runmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 3:
				iv = currnode->value.intval;
				if (abs(iv) > 100)
					goto invaliddata;
				hcircuit->set.ambient_factor = iv;
				break;
			case 4:
				fv = currnode->value.floatval;
				if (fv < 0)
					goto invaliddata;
				hcircuit->set.wtemp_rorh = deltaK_to_temp(fv);
				break;
			case 5:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				hcircuit->set.am_tambient_tK = iv;
				break;
			case 6:
				fv = currnode->value.floatval;
				hcircuit->set.tambient_boostdelta = deltaK_to_temp(fv);	// allow negative values because why not
				break;
			case 7:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				hcircuit->set.boost_maxtime = iv;
				break;
			case 8:
				if (ALL_OK != tid_parse(&hcircuit->set.tid_outgoing, currnode))
					goto invaliddata;
				break;
			case 9:
				if (ALL_OK != tid_parse(&hcircuit->set.tid_return, currnode))
					goto invaliddata;
				break;
			case 10:
				if (ALL_OK != tid_parse(&hcircuit->set.tid_ambient, currnode))
					goto invaliddata;
				break;
			case 11:
				if (ALL_OK != hcircuit_params_parse(&hcircuit->set.params, currnode))
					goto invaliddata;
				break;
			case 12:
				n = currnode->value.stringval;
				if (!strcmp("bilinear", n))
					ret = hcircuit_tlaw_bilinear_parser(hcircuit, currnode);
				else {
					dbgerr("Unknown %s \"%s\" closing at line %d", currnode->name, n, currnode->lineno);
					return (-EUNKNOWN);
				}
				if (ALL_OK == ret)
					dbgmsg("parsed tlaw \"%s\"", n);
				else
					dbgerr("failed to parse tlaw: %d", ret);
				break;
			case 13:
			case 14:
			case 15:
				n = currnode->value.stringval;
				if (strlen(n) < 1)
					break;	// nothing to do

				switch (i) {
					case 13:
						hcircuit->valve_mix = plant_fbn_valve(plant, n);
						if (!hcircuit->valve_mix)
							goto invaliddata;
						break;
					case 14:
						hcircuit->pump_feed = plant_fbn_pump(plant, n);
						if (!hcircuit->pump_feed)
							goto invaliddata;
						break;
					case 15:
						hcircuit->bmodel = models_fbn_bmodel(n);
						if (!hcircuit->bmodel)
							goto invaliddata;
						break;
					default:
						break;	// should never happen
				}
				dbgmsg("%s: \"%s\" found", currnode->name, n);
				break;
			default:
				break;	// should never happen
		}
	}

	if (ALL_OK == ret)
		hcircuit->set.configured = true;

	return (ret);

invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", currnode->name, currnode->lineno);
	return (-EINVALID);
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", hcircuit_parse));
}

static int plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "pumps", false, pumps_parse, false, NULL, },
		{ NODELST, "valves", false, valves_parse, false, NULL, },
		{ NODELST, "dhwts", false, dhwts_parse, false, NULL, },
		{ NODELST, "hcircuits", false, hcircuits_parse, false, NULL, },
	};
	struct s_plant * plant;
	int ret;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// create a new plant
	plant = plant_new();
	if (!plant) {
		dbgerr("plant creation failed");
		return (-EOOM);
	}

	filecfg_parser_run_parsers(plant, parsers, ARRAY_SIZE(parsers));

	return (0);
}

static int hardware_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
}

/**
 * Match an indidual node against a list of parsers.
 * @param node the target node to match from
 * @param parsers the parsers to match the node with
 * @param nparsers the number of parsers available in parsers[]
 * @return exec status
 */
int filecfg_parser_match_node(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	bool matched = false;
	unsigned int i;

	if (!node || !parsers || !nparsers)
		return (-EINVALID);

	for (i = 0; i < nparsers; i++) {
		if (!(parsers[i].type & node->type))
			continue;	// skip invalid node type

		if (!strcmp(parsers[i].identifier, node->name)) {
			dbgmsg("matched %s, %d", node->name, node->lineno);
			matched = true;
			if (parsers[i].seen) {
				dbgerr("Ignoring duplicate node \"%s\" closing at line %d", node->name, node->lineno);
				continue;
			}
			parsers[i].node = node;
			parsers[i].seen = true;
		}
	}
	if (!matched) {
		// dbgmsg as there can be legit mismatch e.g. when parsing foreign backend config
		dbgmsg("Ignoring unknown node \"%s\" closing at line %d", node->name, node->lineno);
		return (-ENOTFOUND);
	}

	return (ALL_OK);
}

/**
 * Match a set of parsers with a nodelist members.
 * @param nodelist the target nodelist to match from
 * @param parsers the parsers to match with
 * @param nparsers the number of available parsers in parsers[]
 * @return -ENOTFOUND if a required parser didn't match, ALL_OK otherwise
 */
int filecfg_parser_match_nodelist(const struct s_filecfg_parser_nodelist * const nodelist, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	const struct s_filecfg_parser_nodelist *list;
	unsigned int i;
	int ret = ALL_OK;

	// cleanup the parsers before run
	for (i = 0; i < nparsers; i++) {
		parsers[i].seen = false;
		parsers[i].node = NULL;
	}

	// attempt matching
	for (list = nodelist; list; list = list->next)
		filecfg_parser_match_node(list->node, parsers, nparsers);

	// report missing required nodes
	for (i = 0; i < nparsers; i++) {
		if (parsers[i].required && (!parsers[i].seen || !parsers[i].node)) {
			dbgerr("Missing required configuration node \"%s\"", parsers[i].identifier);
			ret = -ENOTFOUND;
		}
	}

	return (ret);
}

/**
 * Trigger all parsers from a parser list.
 * @param priv optional private data
 * @param parsers the parsers to trigger, with their respective .seen and .node elements correctly set
 * @param nparsers the number of parsers available in parsers[]
 */
void filecfg_parser_run_parsers(void * restrict const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	unsigned int i;

	for (i = 0; i < nparsers; i++) {
		if (parsers[i].seen && parsers[i].parser)
			parsers[i].parser(priv, parsers[i].node);
	}
}

int filecfg_parser_process_nodelist(const struct s_filecfg_parser_nodelist *nodelist)
{
	struct s_filecfg_parser_parsers root_parsers[] = {	// order matters we want to parse backends first and plant last
		{ NODELST, "backends", false, hardware_backends_parse, false, NULL, },
		{ NODELST, "defconfig", false, defconfig_parse, false, NULL, },
		{ NODELST, "models", false, models_parse, false, NULL, },
		{ NODELST, "plant", true, plant_parse, false, NULL, },
	};

	printf("\n\nBegin parse\n");
	filecfg_parser_match_nodelist(nodelist, root_parsers, ARRAY_SIZE(root_parsers));
	filecfg_parser_run_parsers(NULL, root_parsers, ARRAY_SIZE(root_parsers));

	return (ALL_OK);
}

/**
 * Free all elements of a nodelist.
 * @param nodelist the target nodelist to purge
 */
void filecfg_parser_free_nodelist(struct s_filecfg_parser_nodelist *nodelist)
{
	struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_nodelist *next;

	if (!nodelist)
		return;

	node = nodelist->node;
	next = nodelist->next;

	free(nodelist);

	if (node) {
		free(node->name);
		if (NODESTR == node->type)
			free(node->value.stringval);
		filecfg_parser_free_nodelist(node->children);
	}

	filecfg_parser_free_nodelist(next);
}
