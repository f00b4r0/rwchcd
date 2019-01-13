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
	extern int yylineno;
	int yylex();
	void yyerror(char *);
%}

%union {
	struct s_filecfg_parser_node *node;
	struct s_filecfg_parser_nodelist *nodelist;
	bool boolval;
	int ival;
	float dval;
	char *strval;
}

%error-verbose
%verbose

%token <boolval> BOOL
%token <ival> INT
%token <dval> FLOAT
%token <strval> IDENTIFIER
%token <strval> STRING

%type <node> node
%type <nodelist> node_list

%%

start: node_list			{ filecfg_parser_process_nodelist($1); }

node_list: /* empty */			{ $$ = NULL; }
	| node_list node		{ $$ = filecfg_parser_new_nodelistelmt($1, $2); }
;

node:	IDENTIFIER BOOL ';'		{ $$ = filecfg_parser_new_node(yylineno, NODEBOOL, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER INT ';'		{ $$ = filecfg_parser_new_node(yylineno, NODEINT, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER FLOAT ';'		{ $$ = filecfg_parser_new_node(yylineno, NODEFLOAT, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER STRING ';'		{ $$ = filecfg_parser_new_node(yylineno, NODESTRING, $1, (u_filecfg_p_nodeval_t)$2, NULL); }
	| IDENTIFIER STRING '{' node_list '}' ';'	{ $$ = filecfg_parser_new_node(yylineno, NODESTRING, $1, (u_filecfg_p_nodeval_t)$2, $4); }
	| IDENTIFIER '{' node_list '}' ';'	{ $$ = filecfg_parser_new_node(yylineno, NODELIST, $1, (u_filecfg_p_nodeval_t)0, $3); }
;

%%


int main(int argc, char **argv)
{
	#ifdef YYDEBUG
	//yydebug = 1;
	#endif
	yyparse();

	return 0;
}

void yyerror(char *s)
{
	fprintf(stderr, "error: line %d: %s\n", yylineno, s);
}
