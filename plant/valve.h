//
//  plant/valve.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Valve operation API.
 */

#ifndef valve_h
#define valve_h

#include "rwchcd.h"
#include "timekeep.h"
#include "inputs.h"
#include "outputs.h"

/** private structure for sapprox valve tcontrol */
struct s_valve_sapprox_priv {
	struct {
		uint_fast16_t amount;		///< amount to move in ‰ (max 1000)
		timekeep_t sample_intvl;	///< sample interval
	} set;		///< settings (externally set)
	struct {
		timekeep_t last_time;		///< last time the sapprox controller was run
	} run;		///< private runtime (internally handled)
};

/** Private structure for PI valve tcontrol */
struct s_valve_pi_priv {
	struct {
		timekeep_t sample_intvl;///< sample interval
		timekeep_t Tu;		///< unit response time
		timekeep_t Td;		///< deadtime
		temp_t Ksmax;		///< maximum valve output delta. Used if it cannot be measured.
		uint_fast8_t tune_f;	///< tuning factor: aggressive: 1 / moderate: 10 / conservative: 100
	} set;		///< settings (externally set)
	struct {
		timekeep_t last_time;	///< last time the PI controller algorithm was run
		timekeep_t Tc;		///< closed loop time constant
		temp_t prev_out;	///< previous run output temperature
		uint_fast32_t Kp_t;	///< Kp time factor: Kp = Kp_t / K, K process gain, Kp proportional coefficient
		int_fast32_t db_acc;	///< deadband accumulator. Needed to integrate when valve is not actuated despite request.
	} run;		///< private runtime (internally handled)
};

/** valve tcontrol algorithm identifiers */
enum e_valve_talgos {
	VA_TALG_NONE = 0,	///< no algorithm, misconfiguration
	VA_TALG_BANGBANG,	///< bangbang controller. Config `bangbang`
	VA_TALG_SAPPROX,	///< sapprox controller. Config `sapprox`
	VA_TALG_PI,		///< PI controller. Config `PI`
};

/** valve motorisation identifiers */
enum e_valve_motor {
	VA_M_NONE = 0,	///< no motor, misconfiguration
	VA_M_3WAY,	///< 3way motor control. Config `3way`
	VA_M_2WAY,	///< 2way motor control. Config `2way`
	//VA_M_10V,
	//VA_M_20MA,
};

/** valve type identifiers */
enum e_valve_type {
	VA_TYPE_NONE = 0,	///< no type, misconfiguration
	VA_TYPE_MIX,		///< mixing type. Config `mix`
	VA_TYPE_ISOL,		///< isolation type. Config `isol`
	VA_TYPE_UNKNOWN,	///< invalid past this value
};

/** Private structure for 3way motorisation settings */
struct s_valve_motor_3way_set {
	orid_t rid_open;	///< relay for opening the valve
	orid_t rid_close;	///< relay for closing the valve
};

/** Private structure for 2way motorisation settings */
struct s_valve_motor_2way_set {
	orid_t rid_trigger;	///< relay for triggering the motor
	bool trigger_opens;	///< true if the trigger opens the valve (false if the trigger closes the valve)
};

/** Union for valve motorisation settings */
union u_valve_motor_set {
	struct s_valve_motor_3way_set m3way;	///< 3way motorisation settings
	struct s_valve_motor_2way_set m2way;	///< 2way motorisation settings
};

/** Private structure for mixing type valve */
struct s_valve_type_mix_set {
	temp_t tdeadzone;	///< valve deadzone: no operation when target temp in deadzone
	itid_t tid_hot;		///< temp at the "hot" input: when position is 0% (closed) there is 0% flow from this input
	itid_t tid_cold;	///< temp at the "cold" input: when position is 0% (closed) there is 100% flow from this input
	itid_t tid_out;		///< temp at the output
	enum e_valve_talgos algo;///< valve tcontrol algorithm identifier
};

/** Private structure for isolation type valve */
struct s_valve_type_isol_set {
	bool reverse;		///< true if opening the valve isolates the target
};

/** Union for valve type settings */
union u_valve_type_set {
	struct s_valve_type_mix_set tmix;	///< mixing valve settings
	struct s_valve_type_isol_set tisol;	///< isolation valve settings
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
/** Valve element structure */
struct s_valve {
	struct {
		bool configured;	///< true if properly configured
		uint_fast16_t deadband;	///< deadband for valve operation in ‰: no operation if requested move is less than that
		timekeep_t ete_time;	///< end-to-end run time
		enum e_valve_type type;	///< type of valve
		enum e_valve_motor motor;	///< type of motor
		union u_valve_type_set tset;	///< type configuration data
		union u_valve_motor_set mset;	///< motor configuration data
	} set;		///< settings (externally set)
	struct {
		bool online;		///< true if valve is operational (under software management)
		bool active;		///< true if valve is in active (in use by the system)
		bool dwht_use;		///< true if valve is currently used by active DHWT
		bool true_pos;		///< true if current position is "true": position measured from a full close/open start, or provided by a sensor
		bool ctrl_ready;	///< false if controller algorithm must be reset
		int_fast16_t actual_position;	///< current position in ‰
		int_fast16_t target_course;	///< current target course in ‰ of set.ete_time
		timekeep_t acc_open_time;	///< accumulated open time since last close
		timekeep_t acc_close_time;	///< accumulated close time since last open
		timekeep_t last_run_time;	///< last time valve_run() was invoked
		enum { STOP = 0, OPEN, CLOSE } actual_action,	///< current valve action
					       request_action;	///< requested action
	} run;		///< private runtime (internally handled)
	const char * restrict name;	///< unique valve name
	void * restrict priv;		///< private data
};

struct s_valve * valve_new(void) __attribute__((warn_unused_result));
void valve_del(struct s_valve * valve);
int valve_online(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_shutdown(struct s_valve * const valve);
int valve_offline(struct s_valve * const valve);
int valve_run(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_reqstop(struct s_valve * const valve);
int valve_request_pth(struct s_valve * const valve, int_fast16_t perth);
int valve_make_bangbang(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_make_sapprox(struct s_valve * const valve, uint_fast16_t amount, timekeep_t intvl) __attribute__((warn_unused_result));
int valve_make_pi(struct s_valve * const valve, timekeep_t intvl, timekeep_t Td, timekeep_t Tu, temp_t Ksmax, uint_fast8_t t_factor) __attribute__((warn_unused_result));
int valve_mix_tcontrol(struct s_valve * const valve, const temp_t target_tout) __attribute__((warn_unused_result));
int valve_isol_trigger(struct s_valve * const valve, bool isolate) __attribute__((warn_unused_result));

#define VALVE_REQMAXPTH			1200	///< request value for full open/close state
#define valve_reqopen_full(valve)	valve_request_pth(valve, VALVE_REQMAXPTH)	///< request valve full open
#define valve_reqclose_full(valve)	valve_request_pth(valve, -VALVE_REQMAXPTH)	///< request valve full close

#endif /* valve_h */
