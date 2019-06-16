//
//  filecfg_parser.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File config parser API.
 */

#ifndef filecfg_parser_h
#define filecfg_parser_h

#include <stdbool.h>

/** Union for node value */
union u_filecfg_parser_nodeval {
	bool boolval;
	int intval;
	float floatval;
	char *stringval;
};

/** Node value union type */
typedef union u_filecfg_parser_nodeval u_filecfg_p_nodeval_t;

/** Valid node types, value used as bitfield */
enum e_filecfg_nodetype {
	NODEBOL = 0x01,		///< Boolean node
	NODEINT = 0x02,		///< Integer node
	NODEFLT = 0x04,		///< Float node
	NODESTR = 0x08,		///< String node
	NODELST = 0x10,		///< List node
};

/** Config node structure */
struct s_filecfg_parser_node {
	int lineno;					///< Line number for this node
	enum e_filecfg_nodetype type;			///< Type of this node
	char * name;					///< Name of this node
	union u_filecfg_parser_nodeval value;		///< Value of this node
	struct s_filecfg_parser_nodelist *children;	///< Children of this node (if any)
};

/** Parser function type */
typedef int (* const parser_t)(void * restrict const priv, const struct s_filecfg_parser_node * const);

/** Structure for linked list of nodes */
struct s_filecfg_parser_nodelist {
	struct s_filecfg_parser_node *node;		///< current node
	struct s_filecfg_parser_nodelist *next;		///< next list member
};

/** Structure for node parsers */
struct s_filecfg_parser_parsers {
	const enum e_filecfg_nodetype type;		///< Expected node type for this parser
	const char * const identifier;			///< Expected node name for this parser
	const bool required;				///< True if node is required to exist
	parser_t parser;				///< node data parser callback
	// the next element will be dynamically updated by filecfg_parser_match_*()
	const struct s_filecfg_parser_node *node;	///< Pointer to matched node
};

struct s_filecfg_parser_node * filecfg_parser_new_node(int lineno, int type, char *name, union u_filecfg_parser_nodeval value, struct s_filecfg_parser_nodelist *children);
struct s_filecfg_parser_nodelist * filecfg_parser_new_nodelistelmt(struct s_filecfg_parser_nodelist *next, struct s_filecfg_parser_node *node);

int filecfg_parser_process_config(const struct s_filecfg_parser_nodelist *nodelist);
void filecfg_parser_free_nodelist(struct s_filecfg_parser_nodelist *nodelist);
int filecfg_parser_match_node(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
int filecfg_parser_match_nodelist(const struct s_filecfg_parser_nodelist * const nodelist, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
int filecfg_parser_match_nodechildren(const struct s_filecfg_parser_node * const node, struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
int filecfg_parser_run_parsers(void * restrict const priv, const struct s_filecfg_parser_parsers parsers[], const unsigned int nparsers);
int filecfg_parser_parse_siblings(void * restrict const priv, const struct s_filecfg_parser_nodelist * const nodelist, const char * nname, const enum e_filecfg_nodetype ntype, const parser_t parser);

/// Custom pr_err for configuration problems.
#define filecfg_parser_pr_err(format, ...)		fprintf(stderr, "CONFIG ERROR! " format "\n", ## __VA_ARGS__)

/**
 * Report (generic) invalid data for node.
 * @param NODE the faulty node
 */
#define filecfg_parser_report_invaliddata(NODE)		filecfg_parser_pr_err("Invalid data for node \"%s\" closing at line %d", NODE->name, NODE->lineno)

/**
 * Parse a list of "named" sibling nodes (String nodes).
 * @param priv optional private data
 * @param nodelist the list of sibling nodes
 * @param nname the expected name for sibling nodes
 * @param parser the parser to apply to each sibling node
 * @return exec status
 */
#define filecfg_parser_parse_namedsiblings(priv, nodelist, nname, parser)	filecfg_parser_parse_siblings(priv, nodelist, nname, NODESTR, parser)

/**
 * Parse a list of "anonymous" sibling nodes (List nodes).
 * @param priv optional private data
 * @param nodelist the list of sibling nodes
 * @param nname the expected name for sibling nodes
 * @param parser the parser to apply to each sibling node
 * @return exec status
 */
#define filecfg_parser_parse_listsiblings(priv, nodelist, nname, parser)	filecfg_parser_parse_siblings(priv, nodelist, nname, NODELST, parser)

#endif /* filecfg_parser_h */
