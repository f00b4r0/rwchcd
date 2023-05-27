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

/** Inputs internal data */
struct s_inputs {
	struct {
		inid_t n;			///< number of allocated temperature inputs
		inid_t last;			///< id of last free slot
		struct s_temperature * all;	///< pointer to dynamically allocated array of temperature inputs
	} temps;	///< temperature inputs
	//hygros, wind, etc;
};

int inputs_init(void);
int inputs_online(void);
int inputs_temperature_fbn(const char * name);
const char * inputs_temperature_name(const inid_t tid);
int inputs_temperature_get(const inid_t tid, temp_t * const tout) __attribute__((warn_unused_result));
int inputs_temperature_time(const inid_t tid, timekeep_t * const stamp);
int inputs_offline(void);
void inputs_exit(void);

#endif /* inputs_h */
