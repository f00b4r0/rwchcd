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

#include "filecfg_parser.h"

struct s_filecfg_parser_node * filecfg_parser_new_node(int lineno, int type, char *name, union u_filecfg_parser_nodeval value, struct s_filecfg_parser_nodelist *children)
{
	struct s_filecfg_parser_node * node = calloc(1, sizeof(*node));

	if (!node) {
		perror(NULL);
		exit(-1);
	}

	printf("new_node: %d, %d, %s, ", lineno, type, name);
	switch (type) {
		case OPTINT:
		case OPTBOOL:
			printf("%d\n", value.intval);
			break;
		case OPTFLOAT:
			printf("%f\n", value.floatval);
			break;
		case OPTSTRING:
			printf("%s\n", value.stringval);
			break;
		case OPTLIST:
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
	listelmt->option = node;

	return (listelmt);
}

