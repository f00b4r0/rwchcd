//
//  hcircuit.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation API.
 */

#ifndef hcircuit_h
#define hcircuit_h

#include "rwchcd.h"

/** private data for templaw_bilinear (for 20C ambient target) */
struct s_tlaw_bilin20C_priv {
	temp_t tout1;		///< outside temp1 (lowest outdoor temp)
	temp_t twater1;		///< corresponding target water temp1 (highest water temp)
	temp_t tout2;		///< outside temp2 (highest outdoor temp)
	temp_t twater2;		///< corresponding target water temp2 (lowest water temp)
	int_fast16_t nH100;	///< thermal non-linearity coef *100 (e.g. if nH is 1.3, nH100 is 130)
	temp_t toutinfl;	///< outdoor temperature at inflexion point (if 0 will be calculated from nH100)
	temp_t twaterinfl;	///< water temperature at inflexion point (if 0 will be calculated from nH100)
	temp_t offset;		///< global (linear) curve offset
	float slope;		///< global (linear) curve slope
};

/** Heating circuit temperature law identifiers */
enum e_hcircuit_laws {
	HCL_NONE = 0,	///< none, misconfiguration
	HCL_BILINEAR,	///< bilinear temperature law
};

/** Heating circuit element structure */
struct s_hcircuit {
	struct {
		bool configured;		///< true if circuit is configured
		bool fast_cooldown;		///< if true, switching to cooler mode triggers active cooldown (heating is disabled until temperature has cooled to new target)
		bool logging;			///< true if data logging should be enabled for this circuit
		enum e_runmode runmode;		///< current circuit set_runmode
		int_fast16_t ambient_factor;	///< influence of ambient temp on templaw calculations, in percent
		temp_t wtemp_rorh;		///< water temp rate of rise in temp per hour (0 to disable)
		time_t am_tambient_tK;		///< ambient model: time necessary for 1 Kelvin temperature rise (seconds) (0 to disable)
		temp_t tambient_boostdelta;	///< temperature delta applied during boost turn-on (0 to disable)
		time_t boost_maxtime;		///< maximum duration of transition boost
		tempid_t tid_outgoing;		///< outgoing temp sensor id for this circuit
		tempid_t tid_return;		///< return temp sensor id for this circuit
		tempid_t tid_ambient;		///< ambient temp sensor id related to this circuit
		struct s_hcircuit_params params;	///< local parameters overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max.
		enum e_hcircuit_laws tlaw;	///< temperature law identifier
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if circuit is operational (under software management)
		bool active;			///< true if circuit is active
		bool outhoff;			///< true if no heating conditions are met
		bool floor_output;		///< true if the current output should not be reduced
		time_t last_run_time;		///< last time hcircuit_run() was invoked. XXX not implemented
		enum e_runmode runmode;		///< circuit actual (computed) runmode
		temp_t rorh_last_target;	///< previous set point target for rorh control
		time_t rorh_update_time;	///< last time output was updated with respect to rorh
		temp_t request_ambient;		///< current requested ambient target temp
		temp_t target_ambient;		///< current calculated ambient target temp (includes offset and computed shifts)
		enum { TRANS_NONE = 0, TRANS_UP, TRANS_DOWN } transition;	///< current transition underwent by the circuit
		time_t ambient_update_time;	///< ambient model last update time
		time_t trans_since;		///< transition start time
		time_t trans_active_elapsed;	///< time elapsed in active transitioning (when power output meats request)
		temp_t trans_start_temp;	///< temperature at transition start
		temp_t actual_ambient;		///< actual ambient temperature (either from sensor, or modelled)
		temp_t target_wtemp;		///< current target water temp
		temp_t actual_wtemp;		///< actual water temperature
		temp_t heat_request;		///< current temp request from heat source for this circuit
	} run;		///< private runtime (internally handled)
	temp_t (*templaw)(const struct s_hcircuit * restrict const, temp_t);	///< pointer to temperature law for this circuit, ref at 20C
	void * restrict tlaw_priv;		///< Reference data for templaw
	struct s_valve * restrict valve_mix;	///< optional valve for circuit (if unavailable -> direct heating)
	struct s_pump * restrict pump_feed;	///< optional pump for this circuit
	const struct s_bmodel * restrict bmodel;///< bmodel corresponding to this circuit
	const struct s_pdata * restrict pdata;	///< read-only plant data for this circuit
	char * restrict name;			///< name for this circuit
};

struct s_hcircuit * hcircuit_new(void) __attribute__((warn_unused_result));
int hcircuit_online(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
int hcircuit_offline(struct s_hcircuit * const circuit);
int hcircuit_run(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
void hcircuit_del(struct s_hcircuit * circuit);

int circuit_make_bilinear(struct s_hcircuit * const circuit, temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, int_fast16_t nH100);

#endif /* circuit_h */
