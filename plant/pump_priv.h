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
		bool shared;			///< true if pump is allowed to be shared between users.
		outid_t rid_pump;		///< relay controlling that pump. *REQUIRED*
	} set;		///< settings (externally set)
	struct {
		atomic_bool online;		///< true if pump is operational (under software management)
		atomic_bool state;		///< actual pump state (only valid for non-shared or master shared)
		bool grabbed;			///< true if pump has been grabbed for use
		bool req_on;			///< request pump on
		bool force_state;		///< true if req_state should be forced (no cooldown)
	} run;		///< private runtime (internally handled)
	struct {
		struct s_pump *parent;		///< parent (master) pump
		struct s_pump *child;		///< list of child pumps
	} virt;		///< associated virtual pumps
	const char * restrict name;	///< unique name for this pump
	enum e_execs status;		///< last known status
};


#endif /* pump_priv_h */
