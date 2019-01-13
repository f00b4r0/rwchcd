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

#include "filecfg_parser.h"

#ifndef ARRAY_SIZE
 #define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))
#endif

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
		case NODEBOOL:
			printf("%d\n", value.intval);
			break;
		case NODEFLOAT:
			printf("%f\n", value.floatval);
			break;
		case NODESTRING:
			printf("%s\n", value.stringval);
			break;
		case NODELIST:
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

struct s_filecfg_parser_parsers {
	const char * identifier;
	bool seen;
	const struct s_filecfg_parser_node *node;
	int (*parser)(const struct s_filecfg_parser_node *);
} Parsers[] = {	// order matters we want to parse backends first and plant last
	{ "backends", false, NULL, NULL, },
	{ "defconfig", false, NULL, NULL, },
	{ "models", false, NULL, NULL, },
	{ "plant", false, NULL, NULL, },
};

int filecfg_parser_process_nodelist(const struct s_filecfg_parser_nodelist *nodelist)
{
	const struct s_filecfg_parser_nodelist *list;
	const struct s_filecfg_parser_node *backends, *defconfig, *models, *plant;
	struct s_filecfg_parser_parsers *parser;
	bool matched;
	int i;

	printf("\n\nBegin parse\n");

	for (list = nodelist; list; list = list->next) {
		matched = false;
		printf("seen: %s ", list->node->name);
		for (i = 0; i < ARRAY_SIZE(Parsers); i++) {
			parser = &Parsers[i];
			if (!strcmp(parser->identifier, list->node->name)) {
				printf("matched.\n");
				matched = true;
				parser->node = list->node;
				parser->seen = true;
			}
		}
		if (!matched)
			printf("UNKNOWN!\n");
	}

	for (i = 0; i < ARRAY_SIZE(Parsers); i++) {
		parser = &Parsers[i];
		if (parser->seen && parser->parser) {
			parser->parser(parser->node);
		}
	}

	return 0;
}
