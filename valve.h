//
//  valve.h
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

#include <assert.h>
#include "rwchcd.h"
#include "timekeep.h"

/** private structure for sapprox valve control */
struct s_valve_sapprox_priv {
	struct {
		uint_fast16_t amount;		///< amount to move in ‰
		timekeep_t sample_intvl;	///< sample interval
	} set;		///< settings (externally set)
	struct {
		timekeep_t last_time;		///< last time the sapprox controller was run
	} run;		///< private runtime (internally handled)
};

/** Private structure for PI valve control */
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

/** valve control algorithm identifiers */
enum e_valve_algos {
	VA_NONE = 0,	///< no algorithm, misconfiguration
	VA_BANGBANG,	///< bangbang controller. Config "bangbang"
	VA_SAPPROX,	///< sapprox controller. Config "sapprox"
	VA_PI,		///< PI controller. Config "PI"
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
/** Valve element structure */
struct s_valve {
	struct {
		bool configured;	///< true if properly configured
		temp_t tdeadzone;	///< valve deadzone: no operation when target temp in deadzone
		uint_fast16_t deadband;	///< deadband for valve operation in ‰: no operation if requested move is less than that
		timekeep_t ete_time;	///< end-to-end run time
		tempid_t tid_hot;	///< temp at the "hot" input: when position is 0% (closed) there is 0% flow from this input
		tempid_t tid_cold;	///< temp at the "cold" input: when position is 0% (closed) there is 100% flow from this input
		tempid_t tid_out;	///< temp at the output
		relid_t rid_hot;	///< relay for opening the valve (increase hot input)
		relid_t rid_cold;	///< relay for closing the valve (increase cold input)
		enum e_valve_algos algo;///< valve control algorithm identifier
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
	char * restrict name;	///< valve name
	void * restrict priv;	///< private data structure for cb.control()
	struct {
		int (*control)(struct s_valve * restrict const, const temp_t);	///< pointer to valve controller algorithm
		int (*online)(struct s_valve * restrict const);	///< pointer to valve private online routine (for preflight checks)
	} cb;	///< valve callbacks
};

struct s_valve * valve_new(void) __attribute__((warn_unused_result));
void valve_del(struct s_valve * valve);
int valve_online(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_shutdown(struct s_valve * const valve);
int valve_offline(struct s_valve * const valve);
int valve_logic(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_run(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_reqstop(struct s_valve * const valve);
int valve_request_pth(struct s_valve * const valve, int_fast16_t perth);
int valve_make_bangbang(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_make_sapprox(struct s_valve * const valve, uint_fast8_t amount, timekeep_t intvl) __attribute__((warn_unused_result));
int valve_make_pi(struct s_valve * const valve, timekeep_t intvl, timekeep_t Td, timekeep_t Tu, temp_t Ksmax, uint_fast8_t t_factor) __attribute__((warn_unused_result));

/**
 * Call valve control algorithm based on target temperature.
 * @param valve target valve
 * @param target_tout target temperature at output of valve
 * @return exec status
 */
 __attribute__((warn_unused_result)) static inline int valve_tcontrol(struct s_valve * const valve, const temp_t target_tout)
{
	if (!valve)
		return (-EINVALID);

	if (!valve->run.online)
		return (-EOFFLINE);

	assert(valve->cb.control);
	// apply valve law to determine target position
	return (valve->cb.control(valve, target_tout));
}

#define valve_reqopen_full(valve)	valve_request_pth(valve, 1200)	///< request valve full open
#define valve_reqclose_full(valve)	valve_request_pth(valve, -1200)	///< request valve full close

#endif /* valve_h */
