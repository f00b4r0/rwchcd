//
//  plant/hcircuit.h
//  rwchcd
//
//  (C) 2017,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation API.
 */

#ifndef hcircuit_h
#define hcircuit_h

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

/** Heating circuit element structure */
struct s_hcircuit {
	struct {
		bool configured;		///< true if circuit is configured
		bool fast_cooldown;		///< if true, switching to cooler mode triggers active cooldown (heating is disabled until temperature has cooled to new target). *Defaults to false*
		bool log;			///< true if data logging should be enabled for this circuit. *Defaults to false*
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
		bool online;			///< true if circuit is operational (under software management)
		bool active;			///< true if circuit is active
		bool outhoff;			///< true if no heating conditions are met
		bool floor_output;		///< true if the current output should not be reduced
		_Atomic enum e_runmode runmode;	///< circuit actual (computed) runmode
		enum { TRANS_NONE = 0, TRANS_UP, TRANS_DOWN } transition;	///< current transition underwent by the circuit
		timekeep_t rorh_update_time;	///< last time output was updated with respect to rorh
		timekeep_t ambient_update_time;	///< ambient model last update time
		timekeep_t trans_active_elapsed;///< time elapsed in active transitioning (when power output meats request)
		_Atomic temp_t request_ambient;	///< current requested ambient target temp
		_Atomic temp_t target_ambient;	///< current calculated ambient target temp (includes offset and computed shifts)
		_Atomic temp_t actual_ambient;	///< actual ambient temperature (either from sensor, or modelled)
		_Atomic temp_t target_wtemp;	///< current target water temp
		_Atomic temp_t actual_wtemp;	///< actual water temperature
		_Atomic temp_t heat_request;	///< current temp request from heat source for this circuit
		temp_t rorh_temp_increment;	///< temperature increment for the rorh limiter. Computed once in hcircuit_online()
		temp_t rorh_last_target;	///< previous set point target for rorh control
		temp_t trans_start_temp;	///< temperature at transition start
	} run;		///< private runtime (internally handled)
	void * restrict tlaw_priv;		///< Reference data for templaw
	const struct s_pdata * pdata;		///< read-only plant data for this circuit
	const char * restrict name;		///< unique name for this circuit
	enum e_execs status;			///< last known status
};

int hcircuit_online(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
int hcircuit_offline(struct s_hcircuit * const circuit);
int hcircuit_run(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
void hcircuit_cleanup(struct s_hcircuit * circuit);

int hcircuit_make_bilinear(struct s_hcircuit * const circuit, temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, uint_least16_t nH100);

#endif /* circuit_h */
