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

/** valve type identifiers */
enum e_valve_type {
	VA_TYPE_NONE = 0,	///< no type, misconfiguration
	VA_TYPE_MIX,		///< mixing type. Config `mix`
	VA_TYPE_ISOL,		///< isolation type. Config `isol`. Isolation valve isolates target by closing itself.
	VA_TYPE_UNKNOWN,	///< invalid past this value
};

struct s_valve;

void valve_cleanup(struct s_valve * valve);
int valve_online(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_shutdown(struct s_valve * const valve);
int valve_offline(struct s_valve * const valve);
int valve_run(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_reqstop(struct s_valve * const valve);
int valve_request_pth(struct s_valve * const valve, int_least16_t perth);
int valve_make_bangbang(struct s_valve * const valve) __attribute__((warn_unused_result));
int valve_make_sapprox(struct s_valve * const valve, uint_least16_t amount, timekeep_t intvl) __attribute__((warn_unused_result));
int valve_make_pi(struct s_valve * const valve, timekeep_t intvl, timekeep_t Td, timekeep_t Tu, temp_t Ksmax, uint_least8_t t_factor) __attribute__((warn_unused_result));
int valve_mix_tcontrol(struct s_valve * const valve, const temp_t target_tout) __attribute__((warn_unused_result));
int valve_isol_trigger(struct s_valve * const valve, bool isolate) __attribute__((warn_unused_result));

bool valve_is_online(const struct s_valve * const valve);
bool valve_is_open(const struct s_valve * const valve);
enum e_valve_type valve_get_type(const struct s_valve * const valve);
const char * valve_name(const struct s_valve * const valve);

#define VALVE_REQMAXPTH			1200	///< request value for full open/close state
#define valve_reqopen_full(valve)	valve_request_pth(valve, VALVE_REQMAXPTH)	///< request valve full open
#define valve_reqclose_full(valve)	valve_request_pth(valve, -VALVE_REQMAXPTH)	///< request valve full close

#endif /* valve_h */
