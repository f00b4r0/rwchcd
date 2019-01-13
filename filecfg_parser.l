/*
//  filecfg_parser.l
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
*/

/* https://fossies.org/dox/kea-1.5.0/parser.html
https://gitlab.isc.org/isc-projects/kea/wikis/designs/simple-parser-design
 https://github.com/lijiansong/ConfigFileParser
 */

/* noyywrap disables automatic rewinding for the next file to parse. Since we
 always parse only a single string, there's no need to do any wraps. And
 using yywrap requires linking with -lfl, which provides the default yywrap
 implementation that always returns 1 anyway. */
%option noyywrap

/* nounput simplifies the lexer, by removing support for putting a character
 back into the input stream. We never use such capability anyway. */
%option nounput

/* batch means that we'll never use the generated lexer interactively. */
%option batch

/* Enables debug mode. To see the debug messages, one needs to also set
 yy_flex_debug to 1, then the debug messages will be printed on stderr. */
%option nodebug

%option yylineno

%x COMMENT

IDENTPART		[A-Za-z0-9_]

%{
	#include <stdbool.h>
	#include "filecfg_parser.tab.h"
%}

%%

	/* literal tokens */
"{"			|
"}"			|
";"			{ return filecfg_parser_text[0]; }

	/* comments */
"#".*\n
"//".*\n
"/*"			{ BEGIN(COMMENT); }
<COMMENT>"*/"		{ BEGIN(INITIAL); }
<COMMENT>([^*]|\n)+|.
<COMMENT><<EOF>>	{ printf("%d: Unterminated comment\n", filecfg_parser_lineno); return 0; }

	/* booleans */
true			|
on			|
yes			{ filecfg_parser_lval.boolval=true; return BOOL; }

false			|
off			|
no			{ filecfg_parser_lval.boolval=false; return BOOL; }

	/* integers */
[-+]?[0-9]+		{ filecfg_parser_lval.ival=atoi(filecfg_parser_text); return INT; }

	/* decimals */
[-+]?([0-9]*\.[0-9]+|[0-9]+\.)	{ filecfg_parser_lval.dval=atof(filecfg_parser_text); return FLOAT; }

	/* option identifiers */
[A-Za-z_]{IDENTPART}*	{ filecfg_parser_lval.strval = strdup(filecfg_parser_text); return IDENTIFIER; }

	/* user strings */
'[^'\n]*'		|
\"[^\"\n]*\"		{ filecfg_parser_lval.strval = strdup(filecfg_parser_text+1); filecfg_parser_lval.strval[filecfg_parser_leng-2]='\0'; return STRING; }

[ \t\n]+		/* ignore whitespace */;

.			{ printf("%d: spurious character %c\n", filecfg_parser_lineno, *filecfg_parser_text); return 0; }

<<EOF>>			{ yyterminate(); }
%%
