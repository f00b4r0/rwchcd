//
//  boiler.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Boiler operation API.
 */

#ifndef boiler_h
#define boiler_h

#include "rwchcd.h"
#include "lib.h"	// for s_temp_intgrl
#include "heatsource.h"

/** Boiler heatsource private structure. @todo XXX TODO: return mixing valve / isolation valve / modulating burner */
struct s_boiler_priv {
	struct {
		enum {
			IDLE_NEVER = 0,		///< boiler runs always at least at limit_tmin
			IDLE_FROSTONLY,		///< boiler turns off only in frost free
			IDLE_ALWAYS,		///< boiler turns off any time there's no heat request
		} idle_mode;		///< boiler off regime
		temp_t hysteresis;		///< boiler temp hysteresis
		temp_t limit_thardmax;		///< "safety" trip temperature. Past this temperature the boiler will (be stopped and) require consumers to maximize their usage to dissipate heat faster. @warning required
		temp_t limit_tmax;		///< maximum boiler temp when operating. Must be < limit_thardmax. @warning required
		temp_t limit_tmin;		///< minimum boiler temp when operating
		temp_t limit_treturnmin;	///< minimum boiler return temp (optional)
		temp_t t_freeze;		///< boiler temp trip point for antifreeze (+5C)
		timekeep_t burner_min_time;	///< minimum burner state time (i.e. minimum time spent in either on or off state). Prevents pumping
		tempid_t tid_boiler;		///< boiler temp id
		tempid_t tid_boiler_return;	///< boiler inflow temp id
		relid_t rid_burner_1;		///< first stage of burner
		relid_t rid_burner_2;		///< second stage of burner
	} set;		///< settings (externally set)
	struct {
		bool antifreeze;		///< true if anti freeze tripped
		temp_t target_temp;		///< current target temp
		struct s_temp_intgrl boil_itg;	///< boiler integral (for cold start protection)
		struct s_temp_intgrl ret_itg;	///< return integral (for return temp management)
	} run;		///< private runtime (internally handled)
	struct s_pump * restrict pump_load;	///< load pump for the boiler, if present
	struct s_valve * restrict valve_ret;	///< return valve for the boiler, if present -- XXX NOT IMPLEMENTED
};

int boiler_heatsource(struct s_heatsource * const heat) __attribute__((warn_unused_result));

#endif /* boiler_h */
