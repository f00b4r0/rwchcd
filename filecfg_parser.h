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
	NODEDUR = 0x20,		///< Duration node
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

int filecfg_parser_runmode_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_parser_tid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);
int filecfg_parser_rid_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

int filecfg_parser_get_node_temp(bool positiveonly, bool delta, const struct s_filecfg_parser_node * const n, void *temp);

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

#ifndef ARRAY_SIZE
 #define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))
#endif

#define container_of(ptr, type, member) ({					\
	const typeof( ((type *)0)->member )					\
	*__mptr = (ptr);							\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define pdata_to_plant(_pdata)	container_of(_pdata, struct s_plant, pdata)


#define FILECFG_PARSER_BOOL_PARSE_NEST_FUNC(_struct, _nest, _member)		\
static int fcp_bool_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	s->_nest _member = n->value.boolval;					\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_BOOL_PARSE_SET_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_BOOL_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_BOOL_PARSE_FUNC(_struct, _member)			\
FILECFG_PARSER_BOOL_PARSE_NEST_FUNC(_struct, , _member)

#define FILECFG_PARSER_INT_PARSE_SET_FUNC(_positiveonly, _struct, _setmember)	\
static int fcp_int_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if (_positiveonly && (iv < 0))						\
		return (-EINVALID);						\
	s->set._setmember = iv;							\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_STR_PARSE_SET_FUNC(_nonempty, _struct, _setmember)	\
static int fcp_str_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	const char *str = n->value.stringval;					\
	if (_nonempty && (strlen(str) < 1))					\
		return (-EINVALID);						\
	s->set._setmember = str;						\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_CELSIUS_PARSE_NEST_FUNC(_positiveonly, _delta, _struct, _nest, _member)	\
static int fcp_temp_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int ret; temp_t temp = 0;						\
	ret = filecfg_parser_get_node_temp(_positiveonly, _delta, n, &temp);	\
	s->_nest _member = temp;	/* Note: always set */			\
	return (ret);								\
}

#define FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(_positiveonly, _delta, _struct, _setmember)	\
	FILECFG_PARSER_CELSIUS_PARSE_NEST_FUNC(_positiveonly, _delta, _struct, set., _setmember)

#define FILECFG_PARSER_CELSIUS_PARSE_FUNC(_positiveonly, _delta, _struct, _member)	\
	FILECFG_PARSER_CELSIUS_PARSE_NEST_FUNC(_positiveonly, _delta, _struct, , _member)

#define FILECFG_PARSER_TIME_PARSE_NEST_FUNC(_struct, _nest, _member)		\
static int fcp_tk_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if (iv < 0)								\
		return (-EINVALID);						\
	s->_nest _member = timekeep_sec_to_tk(iv);				\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_TIME_PARSE_SET_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_TIME_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_TIME_PARSE_FUNC(_struct, _member)			\
	FILECFG_PARSER_TIME_PARSE_NEST_FUNC(_struct, , _member)


#define FILECFG_PARSER_TID_PARSE_NEST_FUNC(_struct, _nest, _member)		\
static int fcp_tid_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_parser_tid_parse(&s->_nest _member, n));		\
}

#define FILECFG_PARSER_TID_PARSE_SET_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_TID_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_TID_PARSE_FUNC(_struct, _member)				\
	FILECFG_PARSER_TID_PARSE_NEST_FUNC(_struct, , _setmember)

#define FILECFG_PARSER_RID_PARSE_NEST_FUNC(_struct, _nest, _member)			\
static int fcp_rid_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_parser_rid_parse(&s->_nest _member, n));		\
}

#define FILECFG_PARSER_RID_PARSE_SET_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_RID_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_RID_PARSE_FUNC(_struct, _member)				\
	FILECFG_PARSER_RID_PARSE_NEST_FUNC(_struct, , _setmember)

#define FILECFG_PARSER_PRIO_PARSE_SET_FUNC(_struct, _setmember)			\
static int fcp_prio_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	int iv = n->value.intval;						\
	if ((iv < 0) || (iv > UINT_FAST8_MAX))					\
		return (-EINVALID);						\
	s->set._setmember = (typeof(s->set._setmember))iv;			\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, _nest, _member)		\
static int fcp_runmode_##_struct##_##_member(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	return (filecfg_parser_runmode_parse(&s->_nest _member, n));		\
}

#define FILECFG_PARSER_RUNMODE_PARSE_SET_FUNC(_struct, _setmember)		\
	FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, set., _setmember)

#define FILECFG_PARSER_RUNMODE_PARSE_FUNC(_struct, _setmember)			\
	FILECFG_PARSER_RUNMODE_PARSE_NEST_FUNC(_struct, , _setmember)

#define FILECFG_PARSER_SCHEDID_PARSE_SET_FUNC(_struct, _setmember)		\
static int fcp_schedid_##_struct##_##_setmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv; int iv;			\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	iv = scheduler_schedid_by_name(n->value.stringval);			\
	if (iv <= 0)								\
		return (-EINVALID);						\
	s->set._setmember = (unsigned)iv;					\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_PBMODEL_PARSE_SET_FUNC(_struct, _setpmember)		\
static int fcp_bmodel_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = models_fbn_bmodel(n->value.stringval);		\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_PLANT_PPUMP_PARSE_SET_FUNC(_priv2plant, _struct, _setpmember)		\
static int fcp_pump_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = plant_fbn_pump(_priv2plant(priv), n->value.stringval);	\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
}

#define FILECFG_PARSER_PLANT_PVALVE_PARSE_SET_FUNC(_priv2plant, _struct, _setpmember)		\
static int fcp_valve_##_struct##_p##_setpmember(void * restrict const priv, const struct s_filecfg_parser_node * const n)	\
{										\
	struct _struct * restrict const s = priv;				\
	if (strlen(n->value.stringval) < 1)					\
		return (ALL_OK);	/* nothing to do */			\
	s->set.p._setpmember = plant_fbn_valve(_priv2plant(priv), n->value.stringval);	\
	if (!s->set.p._setpmember)						\
		return (-EINVALID);						\
	return (ALL_OK);							\
}

#endif /* filecfg_parser_h */
