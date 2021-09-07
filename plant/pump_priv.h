//
//  plant/pump_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump internal API.
 */

#ifndef pump_priv_h
#define pump_priv_h

#include <stdatomic.h>
#include "io/outputs.h"
#include "timekeep.h"

/** pump element structure */
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


#endif /* pump_priv_h */
