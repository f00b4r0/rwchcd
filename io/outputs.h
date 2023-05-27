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

typedef uint_fast8_t outid_t;	///< output id

#define OUTID_MAX	UINT_FAST8_MAX

/** Known output types */
enum e_output_type {
	OUTPUT_NONE = 0,	///< output type not configured
	OUTPUT_RELAY,		///< relay output
};

/** Outputs internal data */
struct s_outputs {
	struct {
		outid_t n;		///< number of allocated relay outputs
		outid_t last;		///< id of last free slot
		struct s_relay * all;	///< pointer to dynamically allocated array of relay outputs
	} relays;	///< relay outputs
};

int outputs_init(void);
int outputs_online(void);
int outputs_fbn(const enum e_output_type t, const char * name);
const char * outputs_name(const enum e_output_type t, const outid_t outid);
int outputs_grab(const enum e_output_type t, const outid_t outid);
int outputs_thaw(const enum e_output_type t, const outid_t outid);
int outputs_state_set(const enum e_output_type t, const outid_t outid, const int value) __attribute__((warn_unused_result));
int outputs_state_get(const enum e_output_type t, const outid_t outid);
int outputs_offline(void);
void outputs_exit(void);

#define outputs_relay_fbn(name)			outputs_fbn(OUTPUT_RELAY, name)
#define outputs_relay_name(rid)			outputs_name(OUTPUT_RELAY, rid)
#define outputs_relay_grab(rid)			outputs_grab(OUTPUT_RELAY, rid)
#define outputs_relay_thaw(rid)			outputs_thaw(OUTPUT_RELAY, rid)
#define outputs_relay_state_set(rid, turn_on)	outputs_state_set(OUTPUT_RELAY, rid, turn_on)
#define outputs_relay_state_get(rid)		outputs_state_get(OUTPUT_RELAY, rid)

#endif /* outputs_h */
