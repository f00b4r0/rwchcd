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
 *
 * The configuration format follows an ISC inspired syntax, with ending semicolons
 * after each declaration; and brackets to nest elements in blocks, also terminated with semicolons.
 *
 * The following rules apply:
 * - All options identifiers match the related C-struct field name and are unquoted.
 * - All integer and decimal values must be specified without quotes.
 * - bool options accept one of the following values (lowercase, without quotes):
 * 	- `true`
 * 	- `on`
 * 	- `yes`
 *	- `false`
 * 	- `off`
 *	- `no`
 * - All user strings @b MUST be quoted (single and double quotes accepted).
 * - Comments: to comment the configuration, one can use:
 *	- C++-style "//" single-line comment (all text following will be treated as comment until next line).
 * 	- Perl-style "#" single-line comment (all text following will be treated as comment until next line).
 *	- C-style "/ * ... * /" multi-line comments (all text enclosed between opening '/ *' and closing '* /' will be ignored, even if it spans multiple lines).
 *
 * Type specific rules:
 * - All `timekeep_t` values must be expressed in integer seconds.
 * - All `temp_t` values must be expressed in Celsius degrees (integer or decimal accepted).
 * - All `valves_`, `pump_` and `bmodel` settings expect a quoted string referencing the name of the related item.
 * - All `schedid_t` settings expect a quoted string referencing the name of the target schedule.
 * - All `rid_` and `tid_` are specified as a block specifying the backend name and the name of the relay or sensor within that backend. For instance:
\verbatim
 rid_open {
 	backend "prototype";
 	name "v_open";
 };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>

// forcefully undefine DEBUG to suppress (very verbose) dbgmsg output
#undef DEBUG

#include "hw_backends.h"
#include "config.h"
#include "lib.h"
#include "timekeep.h"
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
#include "heatsource.h"

#include "scheduler.h"

#include "runtime.h"

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
#if 0
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
#endif
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
	int ret = ALL_OK;

#ifdef HAS_HWP1		// XXX
	ret = hw_p1_filecfg_parse(node);
#endif

	return (ret);
}

static int tid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	tempid_t * restrict const tempid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "backend", true, NULL, NULL, },
		{ NODESTR, "name", true, NULL, NULL, },
	};
	const char * backend, * name;
	int ret;

	dbgmsg("Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg("empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	ret = hw_backends_sensor_fbn(tempid, backend, name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or sensor \"%s\" not found"), node->name, node->lineno, backend, name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_sensor_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int rid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	relid_t * restrict const relid = priv;
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "backend", true, NULL, NULL, },
		{ NODESTR, "name", true, NULL, NULL, },
	};
	const char * backend, * name;
	int ret;

	dbgmsg("Trying \"%s\"", node->name);

	// don't report error on empty config
	if (!node->children) {
		dbgmsg("empty");
		return (ALL_OK);
	}

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	backend = parsers[0].node->value.stringval;
	name = parsers[1].node->value.stringval;

	ret = hw_backends_relay_fbn(relid, backend, name);
	switch (ret) {
		case ALL_OK:
			break;
		case -ENOTFOUND:
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: backend \"%s\" and/or relay \"%s\" not found"), node->name, node->lineno, backend, name);
			break;
		default:	// should never happen
			dbgerr("hw_backends_relay_fbn() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int runmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
static int sysmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	enum e_systemmode * restrict const sysmode = priv;
	const char * n;

	n = node->value.stringval;

	if (!strcmp("off", n))
		*sysmode = SYS_OFF;
	else if (!strcmp("auto", n))
		*sysmode = SYS_AUTO;
	else if (!strcmp("comfort", n))
		*sysmode = SYS_COMFORT;
	else if (!strcmp("eco", n))
		*sysmode = SYS_ECO;
	else if (!strcmp("frostfree", n))
		*sysmode = SYS_FROSTFREE;
	else if (!strcmp("test", n))
		*sysmode = SYS_TEST;
	else if (!strcmp("dhwonly", n))
		*sysmode = SYS_DHWONLY;
	else if (!strcmp("manual", n))
		*sysmode = SYS_MANUAL;
	else {
		*sysmode = SYS_UNKNOWN;
		filecfg_parser_pr_err(_("Unknown systemmode \"%s\" at line %d"), n, node->lineno);
		return (-EINVALID);
	}

	return (ALL_OK);
}

static int dhwt_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "t_comfort", false, NULL, NULL, },		// 0
		{ NODEFLT|NODEINT, "t_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "t_frostfree", false, NULL, NULL, },		// 2
		{ NODEFLT|NODEINT, "t_legionella", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_tmin", false, NULL, NULL, },		// 4
		{ NODEFLT|NODEINT, "limit_tmax", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_wintmax", false, NULL, NULL, },	// 6
		{ NODEFLT|NODEINT, "hysteresis", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "temp_inoffset", false, NULL, NULL, },	// 8
		{ NODEINT, "limit_chargetime", false, NULL, NULL, },
	};
	struct s_dhwt_params * restrict const dhwt_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	int iv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		if (NODEFLT == currnode->type) {
			fv = currnode->value.floatval;
			delta = deltaK_to_temp(fv);
			celsius = celsius_to_temp(fv);
		}
		else {	// NODEINT
			iv = currnode->value.intval;
			delta = deltaK_to_temp(iv);
			celsius = celsius_to_temp(iv);
		}

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
				if (delta < 0)
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
					dhwt_params->limit_chargetime = timekeep_sec_to_tk(currnode->value.intval);
				break;
			default:
				break;	// never happen
		}
	}

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int hcircuit_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "t_comfort", false, NULL, NULL, },		// 0
		{ NODEFLT|NODEINT, "t_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "t_frostfree", false, NULL, NULL, },		// 2
		{ NODEFLT|NODEINT, "t_offset", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "outhoff_comfort", false, NULL, NULL, },	// 4
		{ NODEFLT|NODEINT, "outhoff_eco", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "outhoff_frostfree", false, NULL, NULL, },	// 6
		{ NODEFLT|NODEINT, "outhoff_hysteresis", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_wtmin", false, NULL, NULL, },		// 8
		{ NODEFLT|NODEINT, "limit_wtmax", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "temp_inoffset", false, NULL, NULL, },	// 10
	};
	struct s_hcircuit_params * restrict const hcircuit_params = priv;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	float fv;
	int iv;
	temp_t delta, celsius;

	filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		if (NODEFLT == currnode->type) {
			fv = currnode->value.floatval;
			delta = deltaK_to_temp(fv);
			celsius = celsius_to_temp(fv);
		}
		else {	// NODEINT
			iv = currnode->value.intval;
			delta = deltaK_to_temp(iv);
			celsius = celsius_to_temp(iv);
		}

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
				if (delta < 0)
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
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int defconfig_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "summer_maintenance", false, NULL, NULL, },	// 0
		{ NODEBOL, "logging", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_tsummer", false, NULL, NULL, },	// 2
		{ NODEFLT|NODEINT, "limit_tfrost", false, NULL, NULL, },
		{ NODEINT, "sleeping_delay", false, NULL, NULL, },	// 4
		{ NODESTR, "startup_sysmode", true, NULL, NULL, },
		{ NODESTR, "startup_runmode", false, NULL, NULL, },	// 6
		{ NODESTR, "startup_dhwmode", false, NULL, NULL, },
		{ NODELST, "def_hcircuit", false, NULL, NULL, },	// 8
		{ NODELST, "def_dhwt", false, NULL, NULL, },
		{ NODEINT, "summer_run_interval", false, NULL, NULL, },	// 10
		{ NODEINT, "summer_run_duration", false, NULL, NULL, },
	};
	struct s_runtime * const runtime = priv;
	struct s_config * restrict config;
	const struct s_filecfg_parser_node *currnode;
	unsigned int i;
	temp_t celsius;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

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
			case 3:
				celsius = (NODEFLT == currnode->type) ? celsius_to_temp(currnode->value.floatval) : celsius_to_temp(currnode->value.intval);
				switch (i) {
					case 2:
						if (ALL_OK != config_set_tsummer(config, celsius))
							goto invaliddata;
						break;
					case 3:
						if (ALL_OK != config_set_tfrost(config, celsius))
							goto invaliddata;
						break;
					default:
						break;
				}
				break;
			case 4:
			case 10:
			case 11:
				// positive time values
				if (currnode->value.intval < 0)
					goto invaliddata;
				else
					switch (i) {
						case 4:
							config->sleeping_delay = timekeep_sec_to_tk(currnode->value.intval);
							break;
						case 10:
							config->summer_run_interval = timekeep_sec_to_tk(currnode->value.intval);
							break;
						case 11:
							config->summer_run_duration = timekeep_sec_to_tk(currnode->value.intval);
							break;
						default:
							break;
					}
				break;
			case 5:
				ret = sysmode_parse(&config->startup_sysmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 6:
				ret = runmode_parse(&config->startup_runmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 7:
				ret = runmode_parse(&config->startup_dhwmode, currnode);
				if (ALL_OK != ret)
					return (ret);
				break;
			case 8:
				if (ALL_OK != hcircuit_params_parse(&config->def_hcircuit, currnode))
					goto invaliddata;
				break;
			case 9:
				if (ALL_OK != dhwt_params_parse(&config->def_dhwt, currnode))
					goto invaliddata;
				break;
			default:
				break;
		}
	}

	// consistency checks post matching

	if (SYS_MANUAL == config->startup_sysmode) {
		if (!parsers[6].node || !parsers[7].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: startup_sysmode set to \"manual\" but startup_runmode and/or startup_dhwmode are not set"), node->name, node->lineno);
			return (-EINVALID);
		}
	}

	if (config->summer_maintenance) {
		if (!parsers[10].node || !parsers[11].node) {
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: summer_maintenance is set but summer_run_interval and/or summer_run_duration are not set"), node->name, node->lineno);
			return (-EINVALID);

		}
	}
	config->configured = true;
	runtime->config = config;

	// XXX TODO add a "config_validate()" function to validate dhwt/hcircuit defconfig data?
	return (ALL_OK);

	// we choose to interrupt parsing if an error occurs in this function, but let the subparsers run to the end
invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int bmodel_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "logging", false, NULL, NULL, },
		{ NODEINT, "tau", true, NULL, NULL, },
		{ NODELST, "tid_outdoor", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_bmodel * bmodel;
	const char * bmdlname = node->value.stringval;
	int iv, ret;

	// we receive a 'bmodel' node with a valid string attribute which is the bmodel name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	bmodel = models_new_bmodel(bmdlname);
	if (!bmodel)
		return (-EOOM);

	currnode = parsers[0].node;
	if (currnode)
		bmodel->set.logging = currnode->value.boolval;

	currnode = parsers[1].node;
	iv = currnode->value.intval;
	if (iv < 0) {
		filecfg_parser_report_invaliddata(currnode);
		return (-EINVALID);
	}
	bmodel->set.tau = timekeep_sec_to_tk(iv);

	currnode = parsers[2].node;
	if (ALL_OK != tid_parse(&bmodel->set.tid_outdoor, currnode)) {
		filecfg_parser_report_invaliddata(currnode);
		return (-EINVALID);
	}

	bmodel->set.configured = true;

	dbgmsg("matched \"%s\"", bmdlname);

	return (ret);
}

/**
 * Parse a list of sibling nodes.
 * @param priv opaque private data pointer
 * @param nodelist the list of sibling nodes
 * @param nname the expected name for sibling nodes
 * @param ntype the expected type for sibling nodes
 * @param parser the parser to apply to each sibling node
 * @return exec status
 */
int filecfg_parser_parse_siblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist,
				  const char * nname, const enum e_filecfg_nodetype ntype, const parser_t parser)
{
	const struct s_filecfg_parser_nodelist *nlist;
	const struct s_filecfg_parser_node *node;
	const char * sname;
	int ret = -EEMPTY;	// immediate return if nodelist is empty

	for (nlist = nodelist; nlist; nlist = nlist->next) {
		node = nlist->node;
		if (ntype != node->type) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring node \"%s\" with invalid type closing at line %d"), node->name, node->lineno);
			continue;
		}
		if (strcmp(nname, node->name)) {
			fprintf(stderr, _("CONFIG WARNING! Ignoring unknown node \"%s\" closing at line %d"), node->name, node->lineno);
			continue;
		}

		if (NODESTR == ntype) {
			sname = node->value.stringval;

			if (strlen(sname) < 1) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring \"%s\" with empty name closing at line %d"), node->name, node->lineno);
				continue;
			}

			dbgmsg("Trying %s node \"%s\"", node->name, sname);
		}
		else
			dbgmsg("Trying %s node", node->name);

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
		{ NODEINT, "cooldown_time", false, NULL, NULL, },
		{ NODELST, "rid_pump", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_pump * pump;
	int ret = ALL_OK;

	// we receive a 'pump' node with a valid string attribute which is the pump name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the pump
	pump = plant_new_pump(plant, node->value.stringval);
	if (!pump)
		return (-EOOM);

	currnode = parsers[0].node;
	if (currnode) {
		if (currnode->value.intval < 0) {
			ret = -EINVALID;
			goto invaliddata;
		}
		else
			pump->set.cooldown_time = timekeep_sec_to_tk(currnode->value.intval);
	}

	currnode = parsers[1].node;
	ret = rid_parse(&pump->set.rid_pump, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	pump->set.configured = true;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (ret);
}

static int pumps_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "pump", pump_parse));
}

static int valve_algo_sapprox_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sample_intvl", true, NULL, NULL, },
		{ NODEINT, "amount", true, NULL, NULL, },
	};
	struct s_valve * restrict const valve = priv;
	timekeep_t sample_intvl;
	int ret, amount;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	sample_intvl = timekeep_sec_to_tk(parsers[0].node->value.intval);
	amount = parsers[1].node->value.intval;

	ret = valve_make_sapprox(valve, amount, sample_intvl);
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

static int valve_algo_PI_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "sample_intvl", true, NULL, NULL, },
		{ NODEINT, "Tu", true, NULL, NULL, },
		{ NODEINT, "Td", true, NULL, NULL, },
		{ NODEINT, "tune_f", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "Ksmax", true, NULL, NULL, },
	};
	struct s_valve * restrict const valve = priv;
	timekeep_t sample_intvl, Tu, Td;
	int ret, tune_f;
	float Ksmax;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	sample_intvl = timekeep_sec_to_tk(parsers[0].node->value.intval);
	Tu = timekeep_sec_to_tk(parsers[1].node->value.intval);
	Td = timekeep_sec_to_tk(parsers[2].node->value.intval);
	tune_f = parsers[3].node->value.intval;
	Ksmax = (NODEFLT == parsers[4].node->type) ? deltaK_to_temp(parsers[4].node->value.floatval) : deltaK_to_temp(parsers[4].node->value.intval);

	ret = valve_make_pi(valve, sample_intvl, Td, Tu, Ksmax, tune_f);
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

static int valve_tmix_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "tdeadzone", false, NULL, NULL, },	// 0
		{ NODELST, "tid_hot", false, NULL, NULL, },
		{ NODELST, "tid_cold", false, NULL, NULL, },	// 2
		{ NODELST, "tid_out", true, NULL, NULL, },
		{ NODESTR, "algo", true, NULL, NULL, },		// 4
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_valve * restrict const valve = priv;
	const char * n;
	float fv;
	int ret;
	unsigned int i;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	valve->set.type = VA_TYPE_MIX;	// needed by valve_make_* algos

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				fv = (NODEFLT == currnode->type) ? currnode->value.floatval : currnode->value.intval;
				if (fv < 0)
					goto invaliddata;
				else
					valve->set.tset.tmix.tdeadzone = deltaK_to_temp(fv);
				break;
			case 1:
				if (ALL_OK != tid_parse(&valve->set.tset.tmix.tid_hot, currnode))
					goto invaliddata;
				break;
			case 2:
				if (ALL_OK != tid_parse(&valve->set.tset.tmix.tid_cold, currnode))
					goto invaliddata;
				break;
			case 3:
				if (ALL_OK != tid_parse(&valve->set.tset.tmix.tid_out, currnode))
					goto invaliddata;
				break;
			case 4:
				n = currnode->value.stringval;
				if (!strcmp("PI", n))
					ret = valve_algo_PI_parser(valve, currnode);
				else if (!strcmp("sapprox", n))
					ret = valve_algo_sapprox_parser(valve, currnode);
				else if (!strcmp("bangbang", n))
					ret = valve_make_bangbang(valve);
				else
					goto invaliddata;

				if (ALL_OK != ret) {
					valve->set.type = VA_TYPE_NONE;
					return (ret);
				}
				break;
			default:
				break;	// should never happen
		}
	}

	return (ret);

invaliddata:
	valve->set.type = VA_TYPE_NONE;
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int valve_tisol_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "reverse", true, NULL, NULL, },	// 0
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	currnode = parsers[0].node;
	valve->set.tset.tisol.reverse = currnode->value.boolval;

	valve->set.type = VA_TYPE_ISOL;

	return (ret);
}

static int valve_m3way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "rid_open", true, NULL, NULL, },	// 0
		{ NODELST, "rid_close", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	currnode = parsers[0].node;
	ret = rid_parse(&valve->set.mset.m3way.rid_open, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	currnode = parsers[1].node;
	ret = rid_parse(&valve->set.mset.m3way.rid_close, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	if (ALL_OK == ret)
		valve->set.motor = VA_M_3WAY;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int valve_m2way_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "rid_trigger", true, NULL, NULL, },	// 0
		{ NODEBOL, "trigger_opens", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_valve * restrict const valve = priv;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	currnode = parsers[0].node;
	ret = rid_parse(&valve->set.mset.m2way.rid_trigger, currnode);
	if (ALL_OK != ret)
		goto invaliddata;

	currnode = parsers[1].node;
	valve->set.mset.m2way.trigger_opens = currnode->value.boolval;

	if (ALL_OK == ret)
		valve->set.motor = VA_M_2WAY;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int valve_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "deadband", false, NULL, NULL, },	// 0
		{ NODEINT, "ete_time", true, NULL, NULL, },
		{ NODESTR, "type", true, NULL, NULL, },		// 2
		{ NODESTR, "motor", true, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_valve * valve;
	const char * n;
	int iv, ret;
	unsigned int i;

	// we receive a 'valve' node with a valid string attribute which is the valve name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the valve
	valve = plant_new_valve(plant, node->value.stringval);
	if (!valve)
		return (-EOOM);

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
					valve->set.ete_time = timekeep_sec_to_tk(iv);
				break;
			case 2:
				n = currnode->value.stringval;
				if (!strcmp("mix", n))
					ret = valve_tmix_parser(valve, currnode);
				else if (!strcmp("isol", n))
					ret = valve_tisol_parser(valve, currnode);
				else
					goto invaliddata;

				if (ALL_OK != ret)
					return (ret);
				break;
			case 3:
				n = currnode->value.stringval;
				if (!strcmp("3way", n))
					ret = valve_m3way_parser(valve, currnode);
				else if (!strcmp("2way", n))
					ret = valve_m2way_parser(valve, currnode);
				else
					goto invaliddata;

				if (ALL_OK != ret)
					return (ret);
				break;
			default:
				break;	// should never happen
		}
	}

	if (ALL_OK == ret)
		valve->set.configured = true;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
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
		filecfg_parser_pr_err(_("Unknown runmode \"%s\" at line %d"), n, node->lineno);
		return (-EINVALID);
	}

	return (ALL_OK);
}

static int dhwt_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "electric_failover", false, NULL, NULL, },	// 0
		{ NODEBOL, "anti_legionella", false, NULL, NULL, },
		{ NODEBOL, "legionella_recycle", false, NULL, NULL, },	// 2
		{ NODEINT, "prio", false, NULL, NULL, },
		{ NODESTR, "runmode", true, NULL, NULL, },		// 4
		{ NODESTR, "dhwt_cprio", false, NULL, NULL, },
		{ NODESTR, "force_mode", false, NULL, NULL, },		// 6
		{ NODELST, "tid_bottom", false, NULL, NULL, },
		{ NODELST, "tid_top", false, NULL, NULL, },		// 8
		{ NODELST, "tid_win", false, NULL, NULL, },
		{ NODELST, "tid_wout", false, NULL, NULL, },		// 10
		{ NODELST, "rid_selfheater", false, NULL, NULL, },
		{ NODELST, "params", false, NULL, NULL, },		// 12
		{ NODESTR, "pump_feed", false, NULL, NULL, },
		{ NODESTR, "pump_recycle", false, NULL, NULL, },	// 14
		{ NODESTR, "valve_hwisol", false, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_pump * pump;
	struct s_plant * restrict const plant = priv;
	struct s_dhw_tank * dhwt;
	const char * n;
	int iv, ret;
	unsigned int i;

	// we receive a 'dhwt' node with a valid string attribute which is the dhwt name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the dhwt
	dhwt = plant_new_dhwt(plant, node->value.stringval);
	if (!dhwt)
		return (-EOOM);

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
				if (ALL_OK != runmode_parse(&dhwt->set.runmode, currnode))
					goto invaliddata;
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
				else
					goto invaliddata;
				break;
			case 6:
				n = currnode->value.stringval;
				if (!strcmp("never", n))
					dhwt->set.force_mode = DHWTF_NEVER;
				else if (!strcmp("first", n))
					dhwt->set.force_mode = DHWTF_FIRST;
				else if (!strcmp("always", n))
					dhwt->set.force_mode = DHWTF_ALWAYS;
				else
					goto invaliddata;
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
			case 15:
				n = currnode->value.stringval;
				if (strlen(n) < 1)
					break;	// nothing to do

				switch (i) {
					case 13:
					case 14:
						pump = plant_fbn_pump(plant, n);
						if (!pump)
							goto invaliddata;	// pump not found
						if (13 == i)
							dhwt->pump_feed = pump;
						else	// i == 14
							dhwt->pump_recycle = pump;
						break;

					case 15:
						dhwt->valve_hwisol = plant_fbn_valve(plant, n);
						if (!dhwt->valve_hwisol)
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
		dhwt->set.configured = true;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int dhwts_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "dhwt", dhwt_parse));
}

static int hcircuit_tlaw_bilinear_parser(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEFLT|NODEINT, "tout1", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "twater1", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "tout2", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "twater2", true, NULL, NULL, },
		{ NODEINT, "nH100", false, NULL, NULL, },
		// these shouldn't be user-configurable
/*		{ NODEFLT, "toutinfl", false, NULL, NULL, },
		{ NODEFLT, "twaterinfl", false, NULL, NULL, },
		{ NODEFLT, "offset", false, NULL, NULL, },
		{ NODEFLT, "slope", false, NULL, NULL, },*/
	};
	struct s_hcircuit * restrict const hcircuit = priv;
	temp_t tout1, twater1, tout2, twater2;
	int ret, nH100;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	tout1 = (NODEFLT == parsers[0].node->type) ? celsius_to_temp(parsers[0].node->value.floatval) : celsius_to_temp(parsers[0].node->value.intval);
	twater1 = (NODEFLT == parsers[1].node->type) ? celsius_to_temp(parsers[1].node->value.floatval) : celsius_to_temp(parsers[1].node->value.intval);
	tout2 = (NODEFLT == parsers[2].node->type) ? celsius_to_temp(parsers[2].node->value.floatval) : celsius_to_temp(parsers[2].node->value.intval);
	twater2 = (NODEFLT == parsers[3].node->type) ? celsius_to_temp(parsers[3].node->value.floatval) : celsius_to_temp(parsers[3].node->value.intval);
	nH100 = parsers[4].node->value.intval;

	ret = hcircuit_make_bilinear(hcircuit, tout1, twater1, tout2, twater2, nH100);
	switch (ret) {
		case ALL_OK:
			break;
		case -EINVALID:	// we're guaranteed that 'valid' arguments are passed: this error means the configuration is invalid
			filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: invalid configuration settings"), node->name, node->lineno);
			break;
		default:	// should never happen
			dbgerr("hcircuit_make_bilinear() failed with '%d', node \"%s\" closing at line %d", ret, node->name, node->lineno);
			break;
	}

	return (ret);
}

static int hcircuit_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "fast_cooldown", false, NULL, NULL, },	// 0
		{ NODEBOL, "logging", false, NULL, NULL, },
		{ NODESTR, "runmode", true, NULL, NULL, },		// 2
		{ NODEINT, "ambient_factor", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "wtemp_rorh", false, NULL, NULL, },		// 4
		{ NODEINT, "am_tambient_tK", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "tambient_boostdelta", false, NULL, NULL, },	// 6
		{ NODEINT, "boost_maxtime", false, NULL, NULL, },
		{ NODELST, "tid_outgoing", true, NULL, NULL, },		// 8
		{ NODELST, "tid_return", false, NULL, NULL, },
		{ NODELST, "tid_ambient", false, NULL, NULL, },		// 10
		{ NODELST, "params", false, NULL, NULL, },
		{ NODESTR, "tlaw", true, NULL, NULL, },			// 12
		{ NODESTR, "valve_mix", false, NULL, NULL, },
		{ NODESTR, "pump_feed", false, NULL, NULL, },		// 14
		{ NODESTR, "bmodel", false, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_hcircuit * hcircuit;
	const char * n;
	float fv;
	int iv, ret;
	unsigned int i;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the hcircuit
	hcircuit = plant_new_circuit(plant, node->value.stringval);
	if (!hcircuit)
		return (-EOOM);

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
				if (ALL_OK != runmode_parse(&hcircuit->set.runmode, currnode))
					goto invaliddata;
				break;
			case 3:
				iv = currnode->value.intval;
				if (abs(iv) > 100)
					goto invaliddata;
				hcircuit->set.ambient_factor = iv;
				break;
			case 4:
				fv = (NODEFLT == currnode->type) ? currnode->value.floatval : currnode->value.intval;
				if (fv < 0)
					goto invaliddata;
				hcircuit->set.wtemp_rorh = deltaK_to_temp(fv);
				break;
			case 5:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				hcircuit->set.am_tambient_tK = timekeep_sec_to_tk(iv);
				break;
			case 6:
				fv = (NODEFLT == currnode->type) ? currnode->value.floatval : currnode->value.intval;
				hcircuit->set.tambient_boostdelta = deltaK_to_temp(fv);	// allow negative values because why not
				break;
			case 7:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				hcircuit->set.boost_maxtime = timekeep_sec_to_tk(iv);
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
				else
					goto invaliddata;

				if (ALL_OK != ret)
					return (ret);
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
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int hcircuits_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "hcircuit", hcircuit_parse));
}

#include "boiler.h"
static int hs_boiler_parse(const struct s_plant * const plant, struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "idle_mode", false, NULL, NULL, },		// 0
		{ NODEFLT|NODEINT, "hysteresis", true, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_thardmax", true, NULL, NULL, },	// 2
		{ NODEFLT|NODEINT, "limit_tmax", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "limit_tmin", false, NULL, NULL, },		// 4
		{ NODEFLT|NODEINT, "limit_treturnmin", false, NULL, NULL, },
		{ NODEFLT|NODEINT, "t_freeze", true, NULL, NULL, },		// 6
		{ NODEINT, "burner_min_time", false, NULL, NULL, },
		{ NODELST, "tid_boiler", true, NULL, NULL, },		// 8
		{ NODELST, "tid_boiler_return", false, NULL, NULL, },
		{ NODELST, "rid_burner_1", true, NULL, NULL, },		// 10
		{ NODELST, "rid_burner_2", false, NULL, NULL, },
		{ NODESTR, "pump_load", false, NULL, NULL, },		// 12
		{ NODESTR, "valve_ret", false, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_boiler_priv * boiler;
	temp_t temp;
	const char * n;
	float fv;
	int iv, ret;
	unsigned int i;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// make that heatsource a boiler
	ret = boiler_heatsource(heatsource);
	if (ret)
		return (ret);

	// configure that boiler
	boiler = heatsource->priv;

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				n = currnode->value.stringval;
				if (!strcmp("never", n))
					boiler->set.idle_mode = IDLE_NEVER;
				else if (!strcmp("frostonly", n))
					boiler->set.idle_mode = IDLE_FROSTONLY;
				else if (!strcmp("always", n))
					boiler->set.idle_mode = IDLE_ALWAYS;
				else
					goto invaliddata;
				break;
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
				fv = (NODEFLT == currnode->type) ?  currnode->value.floatval : currnode->value.intval;
				if (fv < 0)
					goto invaliddata;
				temp = celsius_to_temp(fv);
				switch (i) {
					case 1:
						boiler->set.hysteresis = deltaK_to_temp(fv);
						break;
					case 2:
						boiler->set.limit_thardmax = temp;
						break;
					case 3:
						boiler->set.limit_tmax = temp;
						break;
					case 4:
						boiler->set.limit_tmin = temp;
						break;
					case 5:
						boiler->set.limit_treturnmin = temp;
						break;
					case 6:
						boiler->set.t_freeze = temp;
						break;
					default:
						break;
				}
				break;
			case 7:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				else
					boiler->set.burner_min_time = timekeep_sec_to_tk(iv);
				break;
			case 8:
				if (ALL_OK != tid_parse(&boiler->set.tid_boiler, currnode))
					goto invaliddata;
				break;
			case 9:
				if (ALL_OK != tid_parse(&boiler->set.tid_boiler_return, currnode))
					goto invaliddata;
				break;
			case 10:
				if (ALL_OK != rid_parse(&boiler->set.rid_burner_1, currnode))
					goto invaliddata;
				break;
			case 11:
				if (ALL_OK != rid_parse(&boiler->set.rid_burner_2, currnode))
					goto invaliddata;
				break;
			case 12:
			case 13:
				n = currnode->value.stringval;
				if (strlen(n) < 1)
					break;	// nothing to do

				switch (i) {
					case 12:
						boiler->pump_load = plant_fbn_pump(plant, n);
						if (!boiler->pump_load)
							goto invaliddata;
						break;
					case 13:
						boiler->valve_ret = plant_fbn_valve(plant, n);
						if (!boiler->valve_ret)
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

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);

}

static int heatsource_type_parse(const struct s_plant * const plant, struct s_heatsource * const heatsource, const struct s_filecfg_parser_node * const node)
{
	int ret;

	if (!strcmp("boiler", node->value.stringval))
		ret = hs_boiler_parse(plant, heatsource, node);
	else
		ret = -EUNKNOWN;

	return (ret);
}

static int heatsource_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "runmode", true, NULL, NULL, },		// 0
		{ NODESTR, "type", true, NULL, NULL, },
		{ NODEINT, "prio", false, NULL, NULL, },			// 2
		{ NODEINT, "consumer_sdelay", false, NULL, NULL, },
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_plant * restrict const plant = priv;
	struct s_heatsource * heatsource;
	int iv, ret;
	unsigned int i;

	// we receive a 'hcircuit' node with a valid string attribute which is the hcircuit name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// create the heatsource
	heatsource = plant_new_heatsource(plant, node->value.stringval);
	if (!heatsource)
		return (-EOOM);

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				if (ALL_OK != runmode_parse(&heatsource->set.runmode, currnode))
					goto invaliddata;
				break;
			case 1:
				if (ALL_OK != heatsource_type_parse(plant, heatsource, currnode))
					goto invaliddata;
				break;
			case 2:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				heatsource->set.prio = iv;
				break;
			case 3:
				iv = currnode->value.intval;
				if (iv < 0)
					goto invaliddata;
				heatsource->set.consumer_sdelay = timekeep_sec_to_tk(iv);
				break;
			default:
				break;	// should never happen
		}
	}

	if (ALL_OK == ret)
		heatsource->set.configured = true;

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int heatsources_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "heatsource", heatsource_parse));
}

static int plant_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "pumps", false, pumps_parse, NULL, },
		{ NODELST, "valves", false, valves_parse, NULL, },
		{ NODELST, "dhwts", false, dhwts_parse, NULL, },
		{ NODELST, "hcircuits", false, hcircuits_parse, NULL, },
		{ NODELST, "heatsources", false, heatsources_parse, NULL, },
	};
	struct s_runtime * const runtime = priv;
	struct s_plant * plant;
	int ret;

	ret = filecfg_parser_match_nodelist(node->children, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	// create a new plant
	plant = plant_new();
	if (!plant)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(plant, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK == ret)
		plant->configured = true;

	runtime->plant = plant;

	return (ret);
}

static int hardware_backends_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", hardware_backend_parse));
}

/* XXX TODO wishlist: parse a single entry spanning multiple weekdays */
static int scheduler_entry_time_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEINT, "wday", true, NULL, NULL, },		// 0
		{ NODEINT, "hour", true, NULL, NULL, },
		{ NODEINT, "min", true, NULL, NULL, },		// 2
	};
	struct s_schedule_etime * const etime = priv;
	const struct s_filecfg_parser_node * currnode;
	unsigned int i;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				if ((currnode->value.intval < 0) || (currnode->value.intval > 7))
					goto invaliddata;
				etime->wday = currnode->value.intval;
				// convert Sunday if necessary
				if (7 == etime->wday)
					etime->wday = 0;
				break;
			case 1:
				if ((currnode->value.intval < 0) || (currnode->value.intval > 23))
					goto invaliddata;
				etime->hour = currnode->value.intval;
				break;
			case 2:
				if ((currnode->value.intval < 0) || (currnode->value.intval > 59))
					goto invaliddata;
				etime->min = currnode->value.intval;
				break;
			default:
				break;	// should never happen
		}
	}

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int scheduler_entry_params_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "runmode", false, NULL, NULL, },	// 0
		{ NODESTR, "dhwmode", false, NULL, NULL, },
		{ NODEBOL, "legionella", false, NULL, NULL, },	// 2
		{ NODEBOL, "recycle", false, NULL, NULL, },
	};
	struct s_schedule_eparams * const eparams = priv;
	const struct s_filecfg_parser_node * currnode;
	unsigned int i;
	int ret;

	// we receive an 'entry' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// reset buffer and set mode defaults
	memset(eparams, 0, sizeof(*eparams));
	eparams->runmode = RM_UNKNOWN;
	eparams->dhwmode = RM_UNKNOWN;

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		switch (i) {
			case 0:
				if (ALL_OK != runmode_parse(&eparams->runmode, currnode))
					goto invaliddata;
				break;
			case 1:
				if (ALL_OK != runmode_parse(&eparams->dhwmode, currnode))
					goto invaliddata;
				break;
			case 2:
				eparams->legionella = currnode->value.boolval;
				break;
			case 3:
				eparams->recycle = currnode->value.boolval;
				break;
			default:
				break;	// should never happen
		}
	}

	return (ret);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (-EINVALID);
}

static int scheduler_entry_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST, "time", true, NULL, NULL, },		// 0
		{ NODELST, "params", true, NULL, NULL, },
	};
	const schedid_t schedid = *(int *)priv;
	struct s_schedule_etime etime;
	struct s_schedule_eparams eparams;
	int ret;

	// we receive an 'entry' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = scheduler_entry_time_parse(&etime, parsers[0].node);
	if (ALL_OK != ret)
		return (ret);

	ret = scheduler_entry_params_parse(&eparams, parsers[1].node);
	if (ALL_OK != ret)
		return (ret);

	ret = scheduler_add_entry(schedid, &etime, &eparams);
	switch (ret) {
		case -EEXISTS:
			filecfg_parser_pr_err(_("Line %d: a schedule entry with the same time is already configured"), node->lineno);
			break;
		default:
			break;
	}

	return (ret);
}

static int scheduler_schedule_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	int schedid;

	if (!node)
		return (-EINVALID);

	if (NODESTR != node->type)
		return (-EINVALID);	// we only accept NODESTR backend node with children

	if (!node->children)
		return (-EEMPTY);
	
	if (strlen(node->value.stringval) <= 0)
		return (-EINVALID);

	schedid = scheduler_add_schedule(node->value.stringval);
	if (schedid <= 0) {
		switch (schedid) {
			case -EEXISTS:
				filecfg_parser_pr_err(_("Line %d: a schedule with the same name (\'%s\') is already configured"), node->lineno, node->value.stringval);
				break;
			default:
				break;
		}
		return (schedid);
	}

	return (filecfg_parser_parse_listsiblings(&schedid, node->children, "entry", scheduler_entry_parse));
}

static int scheduler_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "schedule", scheduler_schedule_parse));
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
		if (!strcmp(parsers[i].identifier, node->name)) {
			if (!(parsers[i].type & node->type)) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring node \"%s\" with invalid type closing at line %d"), node->name, node->lineno);
				return (-EINVALID);
			}

			dbgmsg("matched %s, %d", node->name, node->lineno);
			matched = true;
			if (parsers[i].node) {
				fprintf(stderr, _("CONFIG WARNING! Ignoring duplicate node \"%s\" closing at line %d"), node->name, node->lineno);
				continue;
			}
			parsers[i].node = node;
		}
	}
	if (!matched) {
		// dbgmsg as there can be legit mismatch e.g. when parsing foreign backend config
		dbgmsg("Ignoring unknown node \"%s\" closing at line %d", node->name, node->lineno);
		return (-EUNKNOWN);
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

	// cleanup the parsers nodes before run
	for (i = 0; i < nparsers; i++)
		parsers[i].node = NULL;

	// attempt matching
	for (list = nodelist; list; list = list->next)
		filecfg_parser_match_node(list->node, parsers, nparsers);	// ignore return value to report as many errors as possible at once

	// report missing required nodes
	for (i = 0; i < nparsers; i++) {
		if (parsers[i].required && !parsers[i].node) {
			filecfg_parser_pr_err(_("Missing required configuration node \"%s\""), parsers[i].identifier);
			ret = -ENOTFOUND;
		}
	}

	return (ret);
}

/**
 * Match a set of parsers with a node's children members.
 * @param node the target node to match from children
 * @param parsers the parsers to match with
 * @param nparsers the number of available parsers in parsers[]
 * @return -ENOTFOUND if a required parser didn't match, ALL_OK otherwise
 * @note will report error
 */
int filecfg_parser_match_nodechildren(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	int ret;

	if (!node->children)
		return (-EINVALID);

	ret = filecfg_parser_match_nodelist(node->children, parsers, nparsers);
	if (ALL_OK != ret)
		filecfg_parser_pr_err(_("Incomplete \"%s\" node configuration closing at line %d"), node->name, node->lineno);

	return (ret);
}

/**
 * Trigger all parsers from a parser list.
 * @param priv optional private data
 * @param parsers the parsers to trigger, with their respective .node elements correctly set
 * @param nparsers the number of parsers available in parsers[]
 * @return exec status. @note will abort execution at first error
 */
int filecfg_parser_run_parsers(void * restrict const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	unsigned int i;
	int ret = -EEMPTY;

	for (i = 0; i < nparsers; i++) {
		if (parsers[i].node && parsers[i].parser) {
			dbgmsg("running parser \"%s\"", parsers[i].identifier);
			ret = parsers[i].parser(priv, parsers[i].node);
			if (ALL_OK != ret)
				return (ret);
		}
	}

	return (ret);
}

/**
 * Process the root list of config nodes.
 * This routine is used by the Bison parser.
 * @param nodelist the root nodelist for all the configuration nodes
 * @return 0 on success, 1 on failure
 */
int filecfg_parser_process_config(const struct s_filecfg_parser_nodelist * const nodelist)
{
	struct s_filecfg_parser_parsers root_parsers[] = {	// order matters we want to parse backends first and plant last
		{ NODELST, "backends", false, hardware_backends_parse, NULL, },
		{ NODELST, "defconfig", false, defconfig_parse, NULL, },
		{ NODELST, "models", false, models_parse, NULL, },
		{ NODELST, "plant", true, plant_parse, NULL, },
		{ NODELST, "scheduler", false, scheduler_parse, NULL, },
	};
	struct s_runtime * const runtime = runtime_get();
	int ret;

	pr_log(_("Begin parsing config"));

	if (!nodelist) {
		pr_err("Empty configuration file!");
		return (1);
	}

	ret = filecfg_parser_match_nodelist(nodelist, root_parsers, ARRAY_SIZE(root_parsers));
	if (ALL_OK != ret)
		goto fail;

	ret = filecfg_parser_run_parsers(runtime, root_parsers, ARRAY_SIZE(root_parsers));
	if (ALL_OK != ret)
		goto fail;

	pr_log(_("Config successfully parsed"));
	return (0);

fail:
	switch (ret) {
		case -EOOM:
			pr_err(_("Out of memory while parsing configuration!"));
			break;
		default:
			pr_err(_("Error parsing config! (%d)"), ret);
			break;
	}

	return (1);
}

/**
 * Free all elements of a nodelist.
 * This routine is used by the Bison parser.
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
