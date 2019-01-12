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

struct s_filecfg_opt * filecfg_new_opt(int lineno, int type, char *name, union u_filecfg_optval value)
{
	struct s_filecfg_opt * opt = calloc(1, sizeof(*opt));

	if (!opt) {
		perror(NULL);
		exit(-1);
	}

	printf("new_opt: %d, %d, %s, ", lineno, type, name);
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
		case OPTTIDRID:
			if (value.optlist)
				printf("%s->%s, %s->%s\n", value.optlist->option->name, value.optlist->option->value.stringval, value.optlist->next->option->name, value.optlist->next->option->value.stringval);
			else
				printf("\n");
			break;
		case OPTTYPE:
			printf("{list}\n");
			break;
	}

	opt->lineno = lineno;
	opt->type = type;
	opt->name = name;
	opt->value = value;

	return (opt);
}

struct s_filecfg_optlist * filecfg_new_optlistitem(struct s_filecfg_optlist *next, struct s_filecfg_opt *option)
{
	struct s_filecfg_optlist * listelmt = calloc(1, sizeof(*listelmt));

	if (!listelmt) {
		perror(NULL);
		exit(-1);
	}

	listelmt->next = next;
	listelmt->option = option;

	return (listelmt);
}

