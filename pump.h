//
//  pump.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump operation API.
 */

#ifndef pump_h
#define pump_h

#include "rwchcd.h"

#define FORCE	true
#define NOFORCE	false

/** Pump element structure */
struct s_pump {
	struct {
		bool configured;		///< true if properly configured
		time_t cooldown_time;		///< preset cooldown time during which the pump remains on for transitions from on to off - useful to prevent short runs that might clog the pump
		relid_t rid_relay;		///< hardware relay controlling that pump
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if pump is operational (under software management)
		bool dwht_use;			///< true if pump is currently used by active DHWT
		time_t actual_cooldown_time;	///< actual cooldown time remaining
		bool req_on;			///< request pump on
		bool force_state;		///< true if req_state should be forced (no cooldown)
	} run;		///< private runtime (internally handled)
	char * restrict name;
};

void pump_del(struct s_pump * restrict pump);
int pump_online(struct s_pump * restrict const pump) __attribute__((warn_unused_result));
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state);
int pump_get_state(const struct s_pump * restrict const pump);
int pump_offline(struct s_pump * restrict const pump);
int pump_run(struct s_pump * restrict const pump) __attribute__((warn_unused_result));

#endif /* pump_h */
