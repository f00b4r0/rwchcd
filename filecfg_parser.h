//
//  filecfg_parser.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * filecfg_parser.
 */

#ifndef filecfg_parser_h
#define filecfg_parser_h

#include <stdbool.h>

union u_filecfg_optval {
	bool boolval;
	int intval;
	float floatval;
	char *stringval;
	struct s_filecfg_optlist *optlist;
};

enum e_filecfg_opttype { OPTBOOL, OPTINT, OPTFLOAT, OPTSTRING, OPTTIDRID, OPTTYPE };

struct s_filecfg_opt {
	int lineno;
	enum e_filecfg_opttype type;
	char * name;
	union u_filecfg_optval value;
};

struct s_filecfg_optlist {
	struct s_filecfg_opt *option;
	struct s_filecfg_optlist *next;
};

struct s_filecfg_opt * filecfg_new_opt(int lineno, int type, char *name, union u_filecfg_optval value);
struct s_filecfg_optlist * filecfg_new_optlistitem(struct s_filecfg_optlist *next, struct s_filecfg_opt *option);

#endif /* filecfg_parser_h */
