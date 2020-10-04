//
//  inputs.h
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

typedef uint_fast8_t itid_t;	///< input temperature id

#define ITID_MAX	UINT_FAST8_MAX

/** Inputs internal data */
struct s_inputs {
	struct {
		itid_t n;			///< number of allocated temperature inputs
		itid_t last;			///< id of last free slot
		struct s_temperature * all;	///< pointer to dynamically allocated array of temperature inputs
	} temps;	///< temperature inputs
	//hygros, wind, etc;
};

int inputs_init(void);
int inputs_temperature_fbn(const char * name);
const char * inputs_temperature_name(const itid_t tid);
int inputs_temperature_get(const itid_t tid, temp_t * const tout) __attribute__((warn_unused_result));
int inputs_temperature_time(const itid_t tid, timekeep_t * const stamp);
void inputs_exit(void);

#endif /* inputs_h */
