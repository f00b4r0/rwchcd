%{
	#include <stdio.h>
	#include <stdbool.h>
%}

%union {
	bool boolval;
	int ival;
	float dval;
	char *strval;
}

%error-verbose
%verbose

%token BACKENDS
%token BACKEND
%token CONFIG
%token SENSORS
%token SENSOR
%token RELAYS
%token RELAY
%token <boolval> BOOL
%token <ival> INT
%token <dval> FLOAT
%token <strval> IDENTIFIER
%token <strval> STRING

%%
/* store lineno for each ast to spray error message such as "missing id field for sensor \"foo\" line xx" */

// backends

backends: BACKENDS '{' backend_list '}' ';'	{ printf("new backend list\n"); } ;

backend: BACKEND STRING '{' backend_opts '}' ';' { printf("new backend: %s\n", $2); } ;

backend_opts: /* empty */
	| backend_opts config
	| backend_opts sensors
	| backend_opts relays
;

backend_list: /* empty */
	| backend_list backend
;

config: CONFIG '{' option_list '}' ';' { printf("new config\n"); } ;
sensors: SENSORS '{' sensor_list '}' ';' { printf("new sensor list\n"); } ;
relays: RELAYS '{' relay_list '}' ';' { printf("new relay list\n"); } ;

relay: RELAY STRING '{' option_list '}' ';' { printf("new relay: %s\n", $2); } ;
relay_list: /* empty */
	| relay_list relay
;

sensor: SENSOR STRING '{' option_list '}' ';' { printf("new sensor: %s\n", $2); } ;
sensor_list: /* empty */
	| sensor_list sensor
;

// end backends

option_list: /* empty */
	| option_list option
;

option:	IDENTIFIER BOOL ';'		{ printf("option bool: %s %d\n", $1, $2); }
	| IDENTIFIER INT ';'		{ printf("option int: %s %d\n", $1, $2); }
	| IDENTIFIER FLOAT ';'		{ printf("option float: %s %f\n", $1, $2); }
	| IDENTIFIER STRING ';'		{ printf("option string: %s %s\n", $1, $2); }
;


%%

int main(int argc, char **argv)
{
	#ifdef YYDEBUG
	//yydebug = 1;
	#endif
	yyparse();
}

yyerror(char *s)
{
	fprintf(stderr, "error: %s\n", s);
}
