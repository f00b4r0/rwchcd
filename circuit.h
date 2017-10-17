//
//  circuit.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Circuit operation API.
 */

#ifndef circuit_h
#define circuit_h

#include "rwchcd.h"

/** Heating circuit element structure */
struct s_heating_circuit {
	struct {
		bool configured;		///< true if circuit is configured
		enum e_runmode runmode;		///< current circuit set_runmode
		struct s_circuit_params params;	///< local parameters overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max.
		short ambient_factor;		///< influence of ambient temp on templaw calculations, in percent - XXX REVIEW TYPE
		temp_t wtemp_rorh;		///< water temp rate of rise in temp per hour
		bool fast_cooldown;		///< if true, switching to cooler mode triggers active cooldown (heating is disabled until temperature has cooled to new target)
		time_t am_tambient_tK;		///< ambient model: time necessary for 1 Kelvin temperature rise (seconds)
		temp_t tambient_boostdelta;	///< temperature delta applied during boost turn-on
		time_t max_boost_time;		///< maximum duration of transition boost
		tempid_t id_temp_outgoing;	///< outgoing temp sensor for this circuit
		tempid_t id_temp_return;	///< return temp sensor for this circuit
		tempid_t id_temp_ambient;	///< ambient temp sensor related to this circuit
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if circuit is operational (under software management)
		bool outhoff;			///< true if no heating conditions are met
		bool floor_output;		///< true if the current output should not be reduced
		time_t last_run_time;		///< last time circuit_run() was invoked
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
	temp_t (*templaw)(const struct s_heating_circuit * restrict const, temp_t);	///< pointer to temperature law for this circuit, ref at 20C
	void * restrict tlaw_data_priv;		///< Reference data for templaw
	struct s_valve * restrict valve;	///< valve for circuit (if available, otherwise it's direct)
	struct s_pump * restrict pump;		///< pump for this circuit
	const struct s_bmodel * restrict bmodel;///< bmodel corresponding to this circuit
	char * restrict name;			///< name for this circuit
};

int circuit_online(struct s_heating_circuit * const circuit) __attribute__((warn_unused_result));
int circuit_offline(struct s_heating_circuit * const circuit);
int circuit_run(struct s_heating_circuit * const circuit) __attribute__((warn_unused_result));
void circuit_del(struct s_heating_circuit * circuit);

int circuit_make_bilinear(struct s_heating_circuit * const circuit, temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, int_fast16_t nH100);

#endif /* circuit_h */
