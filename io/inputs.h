//
//  io/inputs.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global inputs interface API.
 */

#ifndef inputs_h
#define inputs_h

#include "rwchcd.h"

typedef uint_fast8_t inid_t;	///< input id

#define INID_MAX	UINT_FAST8_MAX

/** Known input types */
enum e_input_type {
	INPUT_NONE = 0,		///< input type not configured
	INPUT_TEMP,		///< temperature input
	INPUT_SWITCH,		///< switch input
};

/** Inputs internal data */
struct s_inputs {
	struct {
		inid_t n;			///< number of allocated temperature inputs
		inid_t last;			///< id of last free slot
		struct s_temperature * all;	///< pointer to dynamically allocated array of temperature inputs
	} temps;	///< temperature inputs
	struct {
		inid_t n;			///< number of allocated switch inputs
		inid_t last;			///< id of last free slot
		struct s_switch * all;		///< pointer to dynamically allocated array of switch inputs
	} switches;	///< switch inputs
	//hygros, wind, etc;
};

int inputs_init(void);
int inputs_online(void);
int inputs_fbn(const enum e_input_type t, const char * name);
const char * inputs_name(const enum e_input_type t, const inid_t inid);
int inputs_get(const enum e_input_type t, const inid_t inid, void * const outval) __attribute__((warn_unused_result));
int inputs_time(const enum e_input_type t, const inid_t tid, timekeep_t * const stamp);
int inputs_offline(void);
void inputs_exit(void);

#define inputs_temperature_fbn(name)		inputs_fbn(INPUT_TEMP, name)
#define inputs_temperature_name(tid)		inputs_name(INPUT_TEMP, tid)
#define inputs_temperature_get(tid, tout)	inputs_get(INPUT_TEMP, tid, tout)
#define inputs_temperature_time(tid, stamp)	inputs_time(INPUT_TEMP, tid, stamp)

#endif /* inputs_h */
