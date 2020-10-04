//
//  plant/pump.h
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
#include "timekeep.h"
#include "io/outputs.h"

#define FORCE	true	///< to force pump state (bypass cooldown), see pump_set_state()
#define NOFORCE	false	///< to not force pump state (let cooldown operate), see pump_set_state()

/** Pump element structure */
struct s_pump {
	struct {
		bool configured;		///< true if properly configured
		timekeep_t cooldown_time;	///< preset cooldown time during which the pump remains on for transitions from on to off - useful to prevent short runs that might clog the pump
		orid_t rid_pump;		///< relay controlling that pump
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if pump is operational (under software management)
		bool active;			///< true if pump is active (in use by the system)
		bool req_on;			///< request pump on
		bool force_state;		///< true if req_state should be forced (no cooldown)
		bool dwht_use;			///< true if pump is currently used by active DHWT
		timekeep_t last_switch;
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< unique name for this pump
};

struct s_pump * pump_new(void) __attribute__((warn_unused_result));
void pump_del(struct s_pump * restrict pump);
int pump_online(struct s_pump * restrict const pump) __attribute__((warn_unused_result));
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state) __attribute__((warn_unused_result));
int pump_get_state(const struct s_pump * restrict const pump);
int pump_shutdown(struct s_pump * restrict const pump);
int pump_offline(struct s_pump * restrict const pump);
int pump_run(struct s_pump * restrict const pump) __attribute__((warn_unused_result));

#endif /* pump_h */
