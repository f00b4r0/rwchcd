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

union u_filecfg_parser_nodeval {
	bool boolval;
	int intval;
	float floatval;
	char *stringval;
};

typedef union u_filecfg_parser_nodeval u_filecfg_p_nodeval_t;

enum e_filecfg_nodetype { NODEBOL, NODEINT, NODEFLT, NODESTR, NODELST };

struct s_filecfg_parser_node {
	int lineno;
	enum e_filecfg_nodetype type;
	char * name;
	union u_filecfg_parser_nodeval value;
	struct s_filecfg_parser_nodelist *children;
};

struct s_filecfg_parser_nodelist {
	struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_nodelist *next;
};

struct s_filecfg_parser_parsers {
	const enum e_filecfg_nodetype type;
	const char * const identifier;
	const bool required;
	int (* const parser)(void * restrict const priv, const struct s_filecfg_parser_node * const);
	// the next two elements will be dynamically updated
	bool seen;
	const struct s_filecfg_parser_node *node;
};

struct s_filecfg_parser_node * filecfg_parser_new_node(int lineno, int type, char *name, union u_filecfg_parser_nodeval value, struct s_filecfg_parser_nodelist *children);
struct s_filecfg_parser_nodelist * filecfg_parser_new_nodelistelmt(struct s_filecfg_parser_nodelist *next, struct s_filecfg_parser_node *node);
int filecfg_parser_process_nodelist(const struct s_filecfg_parser_nodelist *nodelist);
void filecfg_parser_free_nodelist(struct s_filecfg_parser_nodelist *nodelist);
int filecfg_parser_match_node(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
int filecfg_parser_match_nodelist(const struct s_filecfg_parser_nodelist * const nodelist, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
void filecfg_parser_run_parsers(void * const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);

#endif /* filecfg_parser_h */
