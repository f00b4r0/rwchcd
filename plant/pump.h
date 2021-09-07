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

#include <stdatomic.h>

#include "rwchcd.h"
#include "timekeep.h"
#include "io/outputs.h"

#define FORCE	true	///< to force pump state (bypass cooldown), see pump_set_state()
#define NOFORCE	false	///< to not force pump state (let cooldown operate), see pump_set_state()

/** Pump element structure */
struct s_pump {
	struct {
		bool configured;		///< true if properly configured
		orid_t rid_pump;		///< relay controlling that pump. *REQUIRED*
		timekeep_t cooldown_time;	///< preset cooldown time during which the pump remains on for transitions from on to off. *Optional*, useful to prevent short runs that might clog the pump
	} set;		///< settings (externally set)
	struct {
		atomic_bool online;		///< true if pump is operational (under software management)
		atomic_bool req_on;		///< request pump on
		bool force_state;		///< true if req_state should be forced (no cooldown)
		bool dhwt_use;			///< true if pump is currently used by active DHWT
		timekeep_t last_switch;		///< last time the pump state was toggled
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< unique name for this pump
	enum e_execs status;		///< last known status
};

void pump_cleanup(struct s_pump * restrict pump);
int pump_online(struct s_pump * restrict const pump) __attribute__((warn_unused_result));
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state) __attribute__((warn_unused_result));
int pump_get_state(const struct s_pump * restrict const pump);
int pump_set_dhwt_use(struct s_pump * const pump, bool used);
int pump_get_dhwt_use(const struct s_pump * const pump);
int pump_shutdown(struct s_pump * restrict const pump);
int pump_offline(struct s_pump * restrict const pump);
int pump_run(struct s_pump * restrict const pump) __attribute__((warn_unused_result));

bool pump_is_online(const struct s_pump * const pump);
const char * pump_name(const struct s_pump * const pump);

#endif /* pump_h */
