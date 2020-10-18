//
//  io/outputs.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global outputs interface API.
 */

#ifndef outputs_h
#define outputs_h

#include "rwchcd.h"

typedef uint_fast8_t orid_t;	///< output relay id

#define ORID_MAX	UINT_FAST8_MAX

/** Outputs internal data */
struct s_outputs {
	struct {
		orid_t n;		///< number of allocated relay outputs
		orid_t last;		///< id of last free slot
		struct s_relay * all;	///< pointer to dynamically allocated array of relay outputs
	} relays;	///< relay outputs
};

int outputs_init(void);
int outputs_relay_fbn(const char * name);
const char * outputs_relay_name(const orid_t tid);
int outputs_relay_grab(const orid_t rid);
int outputs_relay_thaw(const orid_t rid);
int outputs_relay_state_set(const orid_t tid, const bool turn_on) __attribute__((warn_unused_result));
int outputs_relay_state_get(const orid_t tid);
void outputs_exit(void);

#endif /* outputs_h */
