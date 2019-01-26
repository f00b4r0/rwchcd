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
	const struct s_filecfg_parser_nodelist *bkdlist;
	const struct s_filecfg_parser_node *bkdnode;
	const char *bkdname = NULL;

	if (!node || !node->children)
		return (-EINVALID);

	for (bkdlist = node->children; bkdlist; bkdlist = bkdlist->next) {
		bkdnode = bkdlist->node;
		if (!bkdnode) {
			printf("invalid node\n");	// xxx assert this can't happen
			continue;
		}

		if (NODESTR != bkdnode->type) {
			dbgerr("Ignoring node \"%s\" with invalid type closing at line %d", bkdnode->name, bkdnode->lineno);
			continue;	// skip invalid node
		}
		if (strcmp("backend", bkdnode->name)) {
			dbgerr("Ignoring unknown node \"%s\" closing at line %d", bkdnode->name, bkdnode->lineno);
			continue;	// skip invalid node
		}

		bkdname = bkdnode->value.stringval;

		if (strlen(bkdname) < 1) {
			dbgerr("Ignoring backend with empty name closing at line %d", bkdnode->lineno);
			continue;
		}

		dbgmsg("Trying %s node \"%s\"", bkdnode->name, bkdname);

		// test backend parsers
		if (ALL_OK == hw_p1_filecfg_parse(bkdnode))	// XXX HACK
			dbgmsg("HW P1 found!");
	}
	return (ALL_OK);
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

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	return (hw_backends_relay_fbn(relid, backend, name));
}

static int def_dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	const struct s_filecfg_parser_nodelist *deflist;
	const struct s_filecfg_parser_node *defnode;
	const char * n;
	float fval;
	int ret = ALL_OK;

	// we only expect to parse floats (or ints that should be floats) and ints
	for (deflist = node->children; deflist; deflist = deflist->next) {
		defnode = deflist->node;

		// use a proxy for float values, needed to parse "expected floats typed as ints"
		if (NODEFLT == defnode->type)
			fval = defnode->value.floatval;
		else if (NODEINT == defnode->type)
			fval = defnode->value.intval;
		else
			goto invalidtype;

		n = defnode->name;

		// test each parameter
		if (!strcmp("t_comfort", n))
			config->def_dhwt.t_comfort = celsius_to_temp(fval);
		else if (!strcmp("t_eco", n))
			config->def_dhwt.t_eco = celsius_to_temp(fval);
		else if (!strcmp("t_frostfree", n))
			config->def_dhwt.t_frostfree = celsius_to_temp(fval);
		else if (!strcmp("t_legionella", n))
			config->def_dhwt.t_legionella = celsius_to_temp(fval);
		else if (!strcmp("limit_tmin", n))
			config->def_dhwt.limit_tmin = celsius_to_temp(fval);
		else if (!strcmp("limit_tmax", n))
			config->def_dhwt.limit_tmax = celsius_to_temp(fval);
		else if (!strcmp("limit_wintmax", n))
			config->def_dhwt.limit_wintmax = celsius_to_temp(fval);
		else if (!strcmp("hysteresis", n))
			config->def_dhwt.hysteresis = deltaK_to_temp(fval);
		else if (!strcmp("temp_inoffset", n))
			config->def_dhwt.temp_inoffset = deltaK_to_temp(fval);
		else if (!strcmp("limit_chargetime", n) && (NODEINT == defnode->type))
			config->def_dhwt.temp_inoffset = deltaK_to_temp(defnode->value.intval);
		else {
invalidtype:
			dbgerr("Ignoring invalid node or node type for \"%s\" closing at line %d", defnode->name, defnode->lineno);
			ret = -EINVALID;
		}
		if (ALL_OK == ret)
			dbgmsg("matched \"%s\": %f", n, fval);
	}

	return (ret);
}

static int def_hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_config * restrict const config = priv;
	const struct s_filecfg_parser_nodelist *deflist;
	const struct s_filecfg_parser_node *defnode;
	const char * n;
	float fval;
	int ret = ALL_OK;

	// we only expect to parse floats (or ints that should be floats)
	for (deflist = node->children; deflist; deflist = deflist->next) {
		defnode = deflist->node;

		// use a proxy for float values, needed to parse "expected floats typed as ints"
		if (NODEFLT == defnode->type)
			fval = defnode->value.floatval;
		else if (NODEINT == defnode->type)
			fval = defnode->value.intval;
		else
			goto invalidtype;

		n = defnode->name;

		// test each parameter
		if (!strcmp("t_comfort", n))
			config->def_hcircuit.t_comfort = celsius_to_temp(fval);
		else if (!strcmp("t_eco", n))
			config->def_hcircuit.t_eco = celsius_to_temp(fval);
		else if (!strcmp("t_frostfree", n))
			config->def_hcircuit.t_frostfree = celsius_to_temp(fval);
		else if (!strcmp("t_offset", n))
			config->def_hcircuit.t_offset = deltaK_to_temp(fval);
		else if (!strcmp("outhoff_comfort", n))
			config->def_hcircuit.outhoff_comfort = celsius_to_temp(fval);
		else if (!strcmp("outhoff_eco", n))
			config->def_hcircuit.outhoff_eco = celsius_to_temp(fval);
		else if (!strcmp("outhoff_frostfree", n))
			config->def_hcircuit.outhoff_frostfree = celsius_to_temp(fval);
		else if (!strcmp("outhoff_hysteresis", n))
			config->def_hcircuit.outhoff_hysteresis = deltaK_to_temp(fval);
		else if (!strcmp("limit_wtmin", n))
			config->def_hcircuit.limit_wtmin = celsius_to_temp(fval);
		else if (!strcmp("limit_wtmax", n))
			config->def_hcircuit.limit_wtmax = celsius_to_temp(fval);
		else if (!strcmp("temp_inoffset", n))
			config->def_hcircuit.temp_inoffset = deltaK_to_temp(fval);
		else {
invalidtype:
			dbgerr("Ignoring invalid node or node type for \"%s\" closing at line %d", defnode->name, defnode->lineno);
			ret = -EINVALID;
		}
		if (ALL_OK == ret)
			dbgmsg("matched \"%s\": %f", n, fval);
	}

	return (ret);
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers def_parsers[] = {
		{ NODELST, "def_hcircuit", false, def_hcircuit_parse, false, NULL, },
		{ NODELST, "def_dhwt", false, def_dhwt_parse, false, NULL, },
	};
	struct s_config * restrict const config = config_new();
	const struct s_filecfg_parser_nodelist *deflist;
	const struct s_filecfg_parser_node *defnode;
	float fval;
	int ret;

	if (!node || !node->children)
		return (-EINVALID);

	for (deflist = node->children; deflist; deflist = deflist->next) {
		defnode = deflist->node;

		// use a proxy for float values, needed to parse "expected floats typed as ints"
		if (NODEFLT == defnode->type)
			fval = defnode->value.floatval;

		switch (defnode->type) {
			case NODEBOL:
				if (!strcmp("summer_maintenance", defnode->name))
					config->summer_maintenance = defnode->value.boolval;
				else if (!strcmp("logging", defnode->name))
					config->logging = defnode->value.boolval;
				else
					goto invalidnode;
				break;
			case NODEINT:
				if (!strcmp("sleeping_delay", defnode->name)) {
					if (defnode->value.intval < 0)
						goto invaliddata;
					else
						config->sleeping_delay = defnode->value.intval;
					break;	// match found
				}
				// attempt to parse int values as float arguments
				fval = defnode->value.intval;
			case NODEFLT:
				if (!strcmp("limit_tsummer", defnode->name))
					ret = config_set_tsummer(config, celsius_to_temp(fval));
				else if (!strcmp("limit_tfrost", defnode->name))
					ret = config_set_tfrost(config, celsius_to_temp(fval));
				else
					goto invalidnode;
				if (ALL_OK != ret)
					goto invaliddata;
				break;
			case NODELST:
				// process def_dhwt and def_hcircuit
				if (ALL_OK != filecfg_parser_match_node(defnode, def_parsers, ARRAY_SIZE(def_parsers)))
					goto invalidnode;
				break;
			case NODESTR:
			default:
				goto invalidnode;
		}
		dbgmsg("matched \"%s\"", defnode->name);
	}
	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
	filecfg_parser_run_parsers(config, def_parsers, ARRAY_SIZE(def_parsers));
	return (ALL_OK);

	// we choose to interrupt parsing if an error occurs in this function, but let the subparsers run to the end
invaliddata:
	dbgerr("Invalid data for node \"%s\" closing at line %d", defnode->name, defnode->lineno);
	return (-EINVALID);

invalidnode:
	dbgerr("Invalid node or node type for \"%s\" closing at line %d", defnode->name, defnode->lineno);
	return (-EINVALID);
}

static int bmodel_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_bmodel * restrict const bmodel = priv;
	const struct s_filecfg_parser_nodelist * nodelist;
	const struct s_filecfg_parser_node * bmdlnode;
	const char * n;
	int ret = ALL_OK;

	for (nodelist = node->children; nodelist; nodelist = nodelist->next) {
		bmdlnode = nodelist->node;
		n = bmdlnode->name;

		// test each parameter
		if ((NODEBOL == bmdlnode->type) && !strcmp("logging", n))
			bmodel->set.logging = bmdlnode->value.boolval;
		else if ((NODEINT == bmdlnode->type) && !strcmp("tau", n)) {
			if (bmdlnode->value.intval < 0) {
				dbgerr("Invalid negative value for \"%s\" closing at line %d", bmdlnode->name, bmdlnode->lineno);
				ret = -EINVALID;
				goto fail;
			}
			else
				bmodel->set.tau = bmdlnode->value.intval;
		}
		else if ((NODELST == bmdlnode->type) && !strcmp("tid_outdoor", n)) {
			ret = tid_parse(&bmodel->set.tid_outdoor, bmdlnode);
			if (ALL_OK != ret) {
				dbgerr("tid_parse failed");
				goto fail;
			}
		}
		else {
			dbgerr("Ignoring invalid node or node type for \"%s\" closing at line %d", bmdlnode->name, bmdlnode->lineno);
			ret = -EUNKNOWN;
		}
		if (ALL_OK == ret)
			dbgmsg("matched \"%s\"", n);
	}
fail:
	return (ret);
}

static int models_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	const struct s_filecfg_parser_nodelist *bmdllist;
	const struct s_filecfg_parser_node *bmdlnode;
	struct s_bmodel * bmodel;
	const char * bmdlname;

	if (!node || !node->children)
		return (-EINVALID);

	for (bmdllist = node->children; bmdllist; bmdllist = bmdllist->next) {
		bmdlnode = bmdllist->node;
		if (strcmp("bmodel", bmdlnode->name)) {
			dbgerr("Ignoring unknown node \"%s\" closing at line %d", bmdlnode->name, bmdlnode->lineno);
			continue;	// skip invalid node
		}
		if (NODESTR != bmdlnode->type) {
			dbgerr("Ignoring node \"%s\" with invalid type closing at line %d", bmdlnode->name, bmdlnode->lineno);
			continue;	// skip invalid node
		}

		bmdlname = bmdlnode->value.stringval;
		if (strlen(bmdlname) < 1) {
			dbgerr("Ignoring bmodel with empty name closing at line %d", bmdlnode->lineno);
			continue;
		}

		dbgmsg("Trying %s node \"%s\"", bmdlnode->name, bmdlname);

		// test bmodel parser
		bmodel = models_new_bmodel(bmdlname);
		if (ALL_OK == bmodel_parse(bmodel, bmdlnode))
			dbgmsg("bmodel \"%s\" found!", bmdlname);
		//else
			//bmodel_del(bmodel);
	}
	return (ALL_OK);
}

#define filecfg_for_node_filter_typename(NODE, TYPE, NAME)							\
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

static int filecfg_parser_parse_namedsiblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist, const char * nname, const parser_t parser)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	const char * sname;
	int ret = -EEMPTY;	// immediate return if nodelist is empty

	for (nlist = nodelist; nlist; nlist = nlist->next) {
		node = nlist->node;
		filecfg_for_node_filter_typename(node, NODESTR, nname);

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

static int plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "pumps", false, pumps_parse, false, NULL, },
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
		if (parsers[i].type != node->type)	// XXX this will enforce floats where ints are input. Possible fix via bitfield instead of enum
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
		{ NODELST, "backends", false, hardware_backend_parse, false, NULL, },
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
