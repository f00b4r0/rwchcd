//
//  filecfg_parser.c
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * filecfg_parser.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "hw_backends.h"
#include "filecfg_parser.h"

#ifdef HAS_HWP1		// XXX
 #include "hw_backends/hw_p1/hw_p1_filecfg.h"
#endif

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

static struct s_filecfg_parser_parsers Parsers[] = {	// order matters we want to parse backends first and plant last
	{ NODELST, "backends", false, hardware_backend_parse, false, NULL, },
	{ NODELST, "defconfig", false, NULL, false, NULL, },
	{ NODELST, "models", false, NULL, false, NULL, },
	{ NODELST, "plant", true, NULL, false, NULL, },
};

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
			dbgmsg("matched %s", node->name);
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
void filecfg_parser_parse_all(void * restrict const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers)
{
	const struct s_filecfg_parser_parsers *parser;
	unsigned int i;

	for (i = 0; i < nparsers; i++) {
		parser = &parsers[i];
		if (parser->seen && parser->parser)
			parser->parser(priv, parser->node);
	}
}

int filecfg_parser_process_nodelist(const struct s_filecfg_parser_nodelist *nodelist)
{
	const struct s_filecfg_parser_nodelist *list;
	const struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_parsers *parser;
	bool matched;
	unsigned int i;

	printf("\n\nBegin parse\n");
	filecfg_parser_match_nodelist(nodelist, Parsers, ARRAY_SIZE(Parsers));
	for (list = nodelist; list; list = list->next) {
		node = list->node;
		if (!node)
			dbgerr("Empty node!");	// xxx assert cannot happen

		if (NODELST != node->type) {
			dbgerr("Ignoring node \"%s\" with invalid type closing at line %d", node->name, node->lineno);
			continue;	// skip invalid node
		}

		matched = false;
		printf("seen: %s ", list->node->name);
		for (i = 0; i < ARRAY_SIZE(Parsers); i++) {
			parser = &Parsers[i];
			if (!strcmp(parser->identifier, list->node->name)) {
				matched = true;
				if (parser->seen) {
					dbgerr("Ignoring duplicate node \"%s\" closing at line %d", node->name, node->lineno);
					continue;
				}
				printf("matched.\n");
				parser->node = list->node;
				parser->seen = true;
			}
		}
		if (!matched)
			dbgerr("Ignoring unknown node \"%s\" closing at line %d", node->name, node->lineno);
	}

	// parse root list in specified order
	for (i = 0; i < ARRAY_SIZE(Parsers); i++) {
		parser = &Parsers[i];
		if (parser->seen && parser->parser) {
			parser->parser(NULL, parser->node);
		}
	}

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
