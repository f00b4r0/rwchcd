//
//  filecfg_parser.y
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

%{
	#include <stdio.h>
	#include "filecfg_parser.h"
	extern int filecfg_parser_lineno;
	int filecfg_parser_lex();
	void filecfg_parser_error(const char *);
%}

%union {
	struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_nodelist *nodelist;
	bool boolval;
	int ival;
	float dval;
	char *strval;
}

%define parse.error verbose
%verbose

%token <boolval> BOOL
%token <ival> INT
%token <dval> FLOAT
%token <strval> IDENTIFIER
%token <strval> STRING

%type <node> node
%type <nodelist> node_list

%%

start: node_list			{ if (filecfg_parser_process_config($1)) YYABORT; filecfg_parser_free_nodelist($1); }

node_list: /* empty */			{ $$ = NULL; }
	| node_list node		{ $$ = filecfg_parser_new_nodelistelmt($1, $2); }
;

node:	IDENTIFIER ';'			{ filecfg_parser_error("missing argument"); YYABORT; }
	| IDENTIFIER BOOL ';'		{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODEBOL, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER INT ';'		{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODEINT, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER FLOAT ';'		{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODEFLT, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER STRING ';'		{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODESTR, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER STRING '{' node_list '}' ';'	{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODESTR, $1, (u_filecfg_p_nodeval_t)$2, $4); }
	| IDENTIFIER '{' node_list '}' ';'	{ $$ = filecfg_parser_new_node(filecfg_parser_lineno, NODELST, $1, (u_filecfg_p_nodeval_t)0, $3); }
;

%%

#if 0
int main(int argc, char **argv)
{
	#ifdef YYDEBUG
	//yydebug = 1;
	#endif
	filecfg_parser_parse();

	return 0;
}
#endif

void filecfg_parser_error(const char *s)
{
	fprintf(stderr, "error: line %d: %s\n", filecfg_parser_lineno, s);
}
