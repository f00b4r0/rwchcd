//
//  plant/valve_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve internal API.
 */

#ifndef valve_priv_h
#define valve_priv_h

#include "valve.h"
#include "timekeep.h"
#include "io/inputs.h"
#include "io/outputs.h"

/** private structure for sapprox valve tcontrol */
struct s_valve_sapprox_priv {
	struct {
		uint_least16_t amount;		///< amount to move in ‰ (max 1000). *REQUIRED*
		timekeep_t sample_intvl;	///< sample interval. *REQUIRED*
	} set;		///< settings (externally set)
	struct {
		timekeep_t last_time;		///< last time the sapprox controller was run
	} run;		///< private runtime (internally handled)
};

/** Private structure for PI valve tcontrol */
struct s_valve_pi_priv {
	struct {
		timekeep_t sample_intvl;///< sample interval. *REQUIRED*
		timekeep_t Tu;		///< unit response time. *REQUIRED*
		timekeep_t Td;		///< deadtime. *REQUIRED*
		temp_t Ksmax;		///< maximum valve output delta. Used if it cannot be measured. *REQUIRED*
		uint_least8_t tune_f;	///< tuning factor: aggressive: 1 / moderate: 10 / conservative: 100. *REQUIRED*
	} set;		///< settings (externally set)
	struct {
		timekeep_t last_time;	///< last time the PI controller algorithm was run
		timekeep_t Tc;		///< closed loop time constant
		temp_t prev_out;	///< previous run output temperature
		uint32_t Kp_t;		///< Kp time factor: Kp = Kp_t / K, K process gain, Kp proportional coefficient
		int32_t db_acc;		///< deadband accumulator. Needed to integrate when valve is not actuated despite request.
	} run;		///< private runtime (internally handled)
};

/** valve tcontrol algorithm identifiers */
enum e_valve_talgos {
	VA_TALG_NONE = 0,	///< no algorithm, misconfiguration
	VA_TALG_BANGBANG,	///< bangbang controller. Config `bangbang`
	VA_TALG_SAPPROX,	///< sapprox controller. Config `sapprox`
	VA_TALG_PI,		///< PI controller. Config `PI`
} ATTRPACK;

/** valve motorisation identifiers */
enum e_valve_motor {
	VA_M_NONE = 0,	///< no motor, misconfiguration
	VA_M_3WAY,	///< 3way motor control. Config `3way`
	VA_M_2WAY,	///< 2way motor control. Config `2way`
	//VA_M_10V,
	//VA_M_20MA,
} ATTRPACK;

/** Private structure for 3way motorisation settings */
struct s_valve_motor_3way_set {
	uint_least16_t deadband;///< deadband for valve operation in ‰: no operation if requested move is less than that. *Optional*
	orid_t rid_open;	///< relay for opening the valve. *REQUIRED*
	orid_t rid_close;	///< relay for closing the valve. *REQUIRED*
};

/** Private structure for 2way motorisation settings */
struct s_valve_motor_2way_set {
	orid_t rid_trigger;	///< relay for triggering the motor. *REQUIRED*
	bool trigger_opens;	///< true if the trigger opens the valve (false if the trigger closes the valve). *REQUIRED*
};

/** Union for valve motorisation settings */
union u_valve_motor_set {
	struct s_valve_motor_3way_set m3way;	///< 3way motorisation settings
	struct s_valve_motor_2way_set m2way;	///< 2way motorisation settings
};

/** Private structure for mixing type valve */
struct s_valve_type_mix_set {
	temp_t tdeadzone;	///< valve deadzone: no operation when target temp in deadzone. *Optional*
	itid_t tid_hot;		///< temp at the "hot" input: when position is 0% (closed) there is 0% flow from this input. *REQUIRED or Optional depending on algorithm*
	itid_t tid_cold;	///< temp at the "cold" input: when position is 0% (closed) there is 100% flow from this input. *Optional*
	itid_t tid_out;		///< temp at the output. *REQUIRED*
	enum e_valve_talgos algo;///< valve tcontrol algorithm identifier. *REQUIRED*
};

/** Union for valve type settings */
union u_valve_type_set {
	struct s_valve_type_mix_set tmix;	///< mixing valve settings
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
/** Valve element structure */
struct s_valve {
	struct {
		bool configured;	///< true if properly configured
		enum e_valve_type type;	///< type of valve. *REQUIRED*
		enum e_valve_motor motor;	///< type of motor. *REQUIRED*
		timekeep_t ete_time;	///< end-to-end run time. *REQUIRED*
		union u_valve_motor_set mset;	///< motor configuration data
		union u_valve_type_set tset;	///< type configuration data
	} set;		///< settings (externally set)
	struct {
		bool online;		///< true if valve is operational (under software management)
		bool true_pos;		///< true if current position is "true": position measured from a full close/open start, or provided by a sensor
		bool ctrl_ready;	///< false if controller algorithm must be reset
		enum { STOP = 0, OPEN, CLOSE } ATTRPACK actual_action,	///< current valve action
							request_action;	///< requested action
		int_least16_t actual_position;	///< current position in ‰
		int_least16_t target_course;	///< current target course in ‰ of #set.ete_time
		timekeep_t acc_open_time;	///< accumulated open time since last close
		timekeep_t acc_close_time;	///< accumulated close time since last open
		timekeep_t last_run_time;	///< last time valve_run() was invoked
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< unique valve name
	void * restrict priv;		///< private data
	enum e_execs status;		///< last known status
};


#endif /* valve_priv_h */
