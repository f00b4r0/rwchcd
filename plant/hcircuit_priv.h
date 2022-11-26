//
//  plant/hcircuit_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit internal API.
 */

#ifndef hcircuit_priv_h
#define hcircuit_priv_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "timekeep.h"
#include "scheduler.h"
#include "io/inputs.h"

/** private data for templaw_bilinear (for 20C ambient target) */
struct s_tlaw_bilin20C_priv {
	struct {
		temp_t tout1;		///< outside temp1 (lowest outdoor temp). *REQUIRED*
		temp_t twater1;		///< corresponding target water temp1 (highest water temp). *REQUIRED*
		temp_t tout2;		///< outside temp2 (highest outdoor temp). *REQUIRED*
		temp_t twater2;		///< corresponding target water temp2 (lowest water temp). *REQUIRED*
		uint_least16_t nH100;	///< thermal non-linearity coef *100 (e.g. if nH is 1.3, nH100 is 130). *REQUIRED*
	} set;
	struct {
		temp_t toutinfl;	///< outdoor temperature at inflexion point (calculated once from nH100 in hcircuit_make_bilinear())
		temp_t twaterinfl;	///< water temperature at inflexion point (calculated once from nH100 in hcircuit_make_bilinear())
	} run;
};

/** Heating circuit temperature law identifiers */
enum e_hcircuit_laws {
	HCL_NONE = 0,	///< none, misconfiguration
	HCL_BILINEAR,	///< bilinear temperature law. Config `bilinear`. *Requires extra parameters, see #s_tlaw_bilin20C_priv)
};

enum e_fastcooldown_modes {
	FCM_NONE	= 0x00,
	FCM_FROSTFREE	= 0x01,
	FCM_ECO		= 0x02,
	FCM_ALL		= 0x03,
};

/** Heating circuit element structure */
struct s_hcircuit {
	struct {
		bool configured;		///< true if circuit is configured
		bool log;			///< true if data logging should be enabled for this circuit. *Defaults to false*
		uint_least8_t fast_cooldown;	///< bitfield used to trigger active cooldown (heating is disabled until temperature has cooled to new target) when switching to specified (cooler) mode. *Defaults to none*
		schedid_t schedid;		///< schedule id for this hcircuit. *Optional*
		enum e_runmode runmode;		///< current circuit set_runmode. *REQUIRED*
		temp_t wtemp_rorh;		///< water temp rate of rise in temp per hour (*default*: 0 disables). *Optional*, requires #p.valve_mix
		temp_t tambient_boostdelta;	///< positive temperature delta applied during boost turn-on (*default*: 0 disables). *Optional*
		timekeep_t boost_maxtime;	///< maximum duration of transition boost. *Optional*
		int_least16_t ambient_factor;	///< influence of ambient temp on templaw calculations, in percent (*default*: 0 disables). *Optional*
		itid_t tid_outgoing;		///< outgoing temp sensor id for this circuit. *REQUIRED*
		itid_t tid_return;		///< return temp sensor id for this circuit. *Optional*
		itid_t tid_ambient;		///< ambient temp sensor id related to this circuit. *Optional*
		enum e_hcircuit_laws tlaw;	///< temperature law identifier. *REQUIRED*
		struct s_hcircuit_params params;///< local parameters overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max. Some settings must be set either globally or locally.
		struct {
			struct s_valve * restrict valve_mix;	///< mixing valve for circuit (if unavailable -> direct heating). *Optional*
			struct s_pump * restrict pump_feed;	///< feed pump for this circuit. *Optional*
			const struct s_bmodel * restrict bmodel;///< Building model assigned to this circuit. *REQUIRED*
		} p;		///< pointer-based settings. For configuration details see specific types instructions
	} set;		///< settings (externally set)
	struct {
		atomic_bool online;		///< true if circuit is operational (under software management)
		bool active;			///< true if circuit is active
		bool outhoff;			///< true if outdoor no heating conditions are met
		bool inoff;			///< true if indoor no heating conditions are met
		bool floor_output;		///< true if the current output should not be reduced
		_Atomic enum e_runmode runmode;	///< circuit actual (computed) runmode
		enum { TRANS_NONE = 0, TRANS_UP, TRANS_DOWN } transition;	///< current temperature transition happening on the circuit
		timekeep_t rorh_update_time;	///< last time output was updated with respect to rorh
		timekeep_t ambient_update_time;	///< ambient model last update time
		timekeep_t trans_start_time;	///< transition start time (may be shifted if power doesn't meet demand)
		_Atomic temp_t request_ambient;	///< current requested ambient target temp (including set offset)
		_Atomic temp_t target_ambient;	///< current calculated ambient target temp (includes computed shift based on actual ambient)
		_Atomic temp_t actual_ambient;	///< actual ambient temperature (either from sensor, or modelled)
		_Atomic temp_t target_wtemp;	///< current target water temp
		_Atomic temp_t actual_wtemp;	///< actual water temperature
		_Atomic temp_t heat_request;	///< current temp request from heat source for this circuit
		temp_t floor_wtemp;		///< saves current wtemp, stops updating when when floor_output is active
		temp_t rorh_temp_increment;	///< temperature increment for the rorh limiter. Computed once in hcircuit_online()
		temp_t rorh_last_target;	///< previous set point target for rorh control
	} run;		///< private runtime (internally handled)
	struct {
		atomic_bool o_runmode;		///< true if set.runmode should be overriden by overrides.runmode
		_Atomic enum e_runmode runmode;	///< runmode override (applied if o_runmode is set)
		_Atomic temp_t t_offset;	///< offset adjustment for ambient targets, applied to all targets
	} overrides;	///< overrides (used for temporary settings override via e.g. dbus calls)
	void * restrict tlaw_priv;		///< Reference data for templaw
	const struct s_pdata * pdata;		///< read-only plant data for this circuit
	const char * restrict name;		///< unique name for this circuit
	enum e_execs status;			///< last known status
};

#endif /* hcircuit_priv_h */
