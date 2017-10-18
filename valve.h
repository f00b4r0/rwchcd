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

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
/** Valve element structure */
struct s_valve {
	struct {
		bool configured;	///< true if properly configured
		temp_t tdeadzone;	///< valve deadzone: no operation when target temp in deadzone
		uint_fast8_t deadband;	///< deadband for valve operation in ‰: no operation if requested move is less than that
		time_t ete_time;	///< end-to-end run time in seconds
		tempid_t id_temp1;	///< temp at the "primary" input: when position is 0% (closed) there is 0% flow from this input
		tempid_t id_temp2;	///< temp at the "secondary" input: when position is 0% (closed) there is 100% flow from this input
		tempid_t id_tempout;	///< temp at the output
		relid_t rid_open;	///< relay for opening the valve
		relid_t rid_close;	///< relay for closing the valve
	} set;		///< settings (externally set)
	struct {
		bool online;		///< true if valve is operational (under software management)
		bool dwht_use;		///< true if valve is currently used by active DHWT
		bool true_pos;		///< true if estimated position is "true": position measured from a full close/open start
		bool ctrl_reset;	///< true if controller algorithm must be reset
		int_fast16_t actual_position;	///< estimated current position in ‰
		int_fast16_t target_course;	///< current target course in ‰ of set.ete_time
		time_t acc_open_time;	///< accumulated open time since last close
		time_t acc_close_time;	///< accumulated close time since last open
		time_t last_run_time;	///< last time valve_run() was invoked
		enum { STOP = 0, OPEN, CLOSE } actual_action,	///< current valve action
		request_action;	///< requested action
	} run;		///< private runtime (internally handled)
	char * restrict name;
	void * restrict priv;	///< private data structure for valvectrl
	int (*valvectrl)(struct s_valve * restrict const, const temp_t);	///< pointer to valve controller algorithm
};

void valve_del(struct s_valve * valve);
int valve_online(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_offline(struct s_valve * const valve);
int valve_logic(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_run(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_reqstop(struct s_valve * const valve);
int valve_request_pth(struct s_valve * const valve, int_fast16_t perth);
int valve_make_bangbang(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_make_sapprox(struct s_valve * const valve, uint_fast8_t amount, time_t intvl) __attribute__((warn_unused_result));
int valve_make_pi(struct s_valve * const valve, time_t intvl, time_t Td, time_t Tu, temp_t Ksmax, uint_fast8_t t_factor) __attribute__((warn_unused_result));

/**
 * Call valve control algorithm based on target temperature.
 * @param valve target valve
 * @param target_tout target temperature at output of valve
 * @return exec status
 */
static inline int valve_control(struct s_valve * const valve, const temp_t target_tout)
{
	assert(valve);
	assert(valve->set.configured);

	// apply valve law to determine target position
	return (valve->valvectrl(valve, target_tout));
}

#define valve_reqopen_full(valve)	valve_request_pth(valve, 1200)	///< request valve full open
#define valve_reqclose_full(valve)	valve_request_pth(valve, -1200)	///< request valve full close


#endif /* valve_h */
