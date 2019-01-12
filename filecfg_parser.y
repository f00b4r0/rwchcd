//
//  filecfg_parser.y
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

%{
	#include <stdio.h>
	#include <stdbool.h>
	#include <string.h>
	#include "filecfg_parser.h"
	extern int yylineno;

%}

%union {
	struct s_filecfg_opt *opt;
	struct s_filecfg_optlist *optlist;
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

%token DEFCONFIG
%token DEF_HCIRCUIT
%token DEF_DHWT

%token MODELS
%token BMODEL

%token PLANT

%token PUMPS
%token PUMP
%token <strval> PUMP_IDENT

%token VALVES
%token VALVE
%token <strval> VALVE_IDENT

%token HEATSOURCES
%token HEATSOURCE

%token HCIRCUITS
%token HCIRCUIT

%token DHWTS
%token DHWT

%token TLAW
%token PARAMS
%token ALGO
%token TYPE
%token NAME

%token <strval> RID_IDENT
%token <strval> TID_IDENT

%token <boolval> BOOL
%token <ival> INT
%token <dval> FLOAT
%token <strval> IDENTIFIER
%token <strval> STRING

%type <opt> option pump_ident valve_ident rid_ident tid_ident type_list
%type <optlist> option_list tidrid_opts

%%
/* store lineno for each ast to spray error message such as "missing id field for sensor \"foo\" line xx" */

nodes: | nodes node;
node : backends | defconfig | models | plant;

// backends

backends: BACKENDS '{' backend_list '}' ';'	{ printf("new backend list\n"); } ;

backend: BACKEND STRING '{' backend_opts '}' ';' { printf("new backend: %s\n", $2); } ;

backend_opts: /* empty */
	| backend_opts type_list
	| backend_opts sensors
	| backend_opts relays
;

backend_list: /* empty */
	| backend_list backend
;

sensors: SENSORS '{' sensor_list '}' ';'	{ printf("new sensor list\n"); } ;
relays: RELAYS '{' relay_list '}' ';'		{ printf("new relay list\n"); } ;

relay: RELAY STRING '{' option_list '}' ';'	{ printf("new relay: %s\n", $2); } ;
relay_list: /* empty */
	| relay_list relay
;

sensor: SENSOR STRING '{' option_list '}' ';'	{ printf("new sensor: %s\n", $2); } ;
sensor_list: /* empty */
	| sensor_list sensor
;

// end backends

// defconfig

defconfig: DEFCONFIG '{'  defconfig_opts '}' ';'	{ printf("new defconfig\n"); } ;
defconfig_opts: option_list
	| defconfig_opts def_hcircuit option_list
	| defconfig_opts def_dhwt option_list
;

def_hcircuit: DEF_HCIRCUIT '{' option_list '}' ';'	{ printf("new def_hcircuit\n"); } ;
def_dhwt: DEF_DHWT '{' option_list '}' ';'		{ printf("new def_dhwt\n"); } ;

// end defconfig

// models

models: MODELS '{' models_list '}' ';'	;

bmodel: BMODEL STRING '{' bmodel_opts '}' ';' ;

bmodel_opts: option_list
	| bmodel_opts tid_ident option_list
;

models_list: /* empty */
	| models_list bmodel
;

// end models

// plant

plant: PLANT '{' plant_opts '}' ';' ;
plant_opts: | plant_opts pumps
	| plant_opts valves
	| plant_opts heatsources
	| plant_opts hcircuits
	| plant_opts dhwts
;

// pumps

pumps: PUMPS '{' pumps_list '}' ';' ;
pump: PUMP STRING '{' pump_opts '}' ';' ;
pump_opts: option_list
	| pump_opts rid_ident option_list
;
pumps_list: /* empty */
	| pumps_list pump
;

// end pumps

// valves

valves: VALVES '{' valves_list '}' ';' ;
valve: VALVE STRING '{' valve_opts '}' ';' ;
valve_opts: option_list
	| valve_opts rid_ident option_list
 	| valve_opts tid_ident option_list
	| valve_opts algo option_list
;
valves_list: /* empty */
	| valves_list valve
;

algo: ALGO STRING '{' option_list '}' ';' ;

// end valves

// heatsources

heatsources: HEATSOURCES '{' heatsources_list '}' ';' ;
heatsource: HEATSOURCE STRING '{' option_list heatsource_type option_list '}' ';' ;
hstype_opts: | hstype_opts option
	| hstype_opts tid_ident
	| hstype_opts rid_ident
	| hstype_opts valve_ident
	| hstype_opts pump_ident
;
heatsource_type: TYPE STRING '{' hstype_opts '}' ';' ;
heatsources_list: | heatsources_list heatsource ;

// end heatsources

// hcircuits

hcircuits: HCIRCUITS '{' hcircuits_list '}' ';' ;
hcircuit: HCIRCUIT STRING '{' hcircuit_opts '}' ';' ;
hcircuit_opts: | hcircuit_opts option
	| hcircuit_opts tid_ident
	| hcircuit_opts rid_ident
	| hcircuit_opts valve_ident
	| hcircuit_opts pump_ident
	| hcircuit_opts PARAMS '{' option_list '}' ';'
	| hcircuit_opts TLAW STRING '{' option_list '}' ';'
	| hcircuit_opts BMODEL STRING ';'
;
hcircuits_list: | hcircuits_list hcircuit;

// end hcircuits

// dhwts

dhwts: DHWTS '{' dhwts_list '}' ';' ;
dhwt: DHWT STRING '{' dhwt_opts '}' ';' ;
dhwt_opts: | dhwt_opts option
	| dhwt_opts tid_ident
	| dhwt_opts rid_ident
	| dhwt_opts pump_ident
	| dhwt_opts PARAMS '{' option_list '}' ';'
;
dhwts_list: | dhwts_list dhwt;

// end dhwts

// end plant

type_list: TYPE STRING '{' option_list '}' ';'	{ $$ = filecfg_new_opt(yylineno, OPTTYPE, $2, (union u_filecfg_optval)$4); } ;

tid_ident: TID_IDENT '{' tidrid_opts '}' ';'	{ $$ = filecfg_new_opt(yylineno, OPTTIDRID, $1, (union u_filecfg_optval)$3); } ;
rid_ident: RID_IDENT '{' tidrid_opts '}' ';'	{ $$ = filecfg_new_opt(yylineno, OPTTIDRID, $1, (union u_filecfg_optval)$3); } ;
pump_ident: PUMP_IDENT STRING ';'		{ $$ = filecfg_new_opt(yylineno, OPTSTRING, $1, (union u_filecfg_optval)$2); } ;
valve_ident: VALVE_IDENT STRING ';' 		{ $$ = filecfg_new_opt(yylineno, OPTSTRING, $1, (union u_filecfg_optval)$2); } ;

tidrid_opts: /* empty */		{ $$ = NULL; }
	| BACKEND STRING ';' NAME STRING ';'	{
						struct s_filecfg_opt * opt1 = filecfg_new_opt(yylineno, OPTSTRING, strdup("backend"), (union u_filecfg_optval)$2);
						struct s_filecfg_optlist *litem = filecfg_new_optlistitem(NULL, opt1);
						struct s_filecfg_opt * opt2 = filecfg_new_opt(yylineno, OPTSTRING, strdup("name"), (union u_filecfg_optval)$5);
						$$ = filecfg_new_optlistitem(litem, opt2);
					}
	| NAME STRING ';' BACKEND STRING ';'	{
						struct s_filecfg_opt * opt1 = filecfg_new_opt(yylineno, OPTSTRING, strdup("name"), (union u_filecfg_optval)$2);
						struct s_filecfg_optlist *litem = filecfg_new_optlistitem(NULL, opt1);
						struct s_filecfg_opt * opt2 = filecfg_new_opt(yylineno, OPTSTRING, strdup("backend"), (union u_filecfg_optval)$5);
						$$ = filecfg_new_optlistitem(litem, opt2);
					}
;

option_list: /* empty */		{ $$ = NULL; }
	| option_list option		{ $$ = filecfg_new_optlistitem($1, $2); }
;

option:	IDENTIFIER BOOL ';'		{ $$ = filecfg_new_opt(yylineno, OPTBOOL, $1, (union u_filecfg_optval)$2); }
	| IDENTIFIER INT ';'		{ $$ = filecfg_new_opt(yylineno, OPTINT, $1, (union u_filecfg_optval)$2); }
	| IDENTIFIER FLOAT ';'		{ $$ = filecfg_new_opt(yylineno, OPTFLOAT, $1, (union u_filecfg_optval)$2); }
	| IDENTIFIER STRING ';'		{ $$ = filecfg_new_opt(yylineno, OPTSTRING, $1, (union u_filecfg_optval)$2); }
	| TYPE STRING ';'		{ $$ = filecfg_new_opt(yylineno, OPTSTRING, strdup("type"), (union u_filecfg_optval)$2); }
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
	fprintf(stderr, "error: line %d: %s\n", yylineno, s);
}
