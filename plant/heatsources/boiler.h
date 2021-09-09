//
//  plant/heatsources/boiler.h
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

#include <stdatomic.h>

#include "rwchcd.h"
#include "lib.h"	// for s_temp_intgrl
#include "plant/heatsource.h"
#include "timekeep.h"
#include "io/inputs.h"
#include "io/outputs.h"

/** Boiler heatsource private structure. @todo XXX TODO: isolation valve / modulating burner */
struct s_boiler_priv {
	struct {
		enum {
			IDLE_NEVER = 0,		///< boiler runs always at least at limit_tmin. Config `never`. *DEFAULT*
			IDLE_FROSTONLY,		///< boiler turns off only in frost free. Config `frostonly`
			IDLE_ALWAYS,		///< boiler turns off any time there's no heat request. Config `always`
		} idle_mode;		///< boiler off regime. *Optional*
		temp_t hysteresis;		///< boiler temp hysteresis. *REQUIRED*
		temp_t limit_thardmax;		///< "safety" trip temperature. *REQUIRED*.  Past this temperature the boiler will (be stopped and) require consumers to maximize their usage to dissipate heat faster.
		temp_t limit_tmax;		///< maximum boiler temp when operating. Must be < (#limit_thardmax - 2K). *Optional, defaults to 90°C*
		temp_t limit_tmin;		///< minimum boiler temp when operating. *Optional, defaults to 10°C*
		temp_t limit_treturnmin;	///< minimum boiler return temp. *Optional*
		temp_t t_freeze;		///< boiler temp trip point for antifreeze. *REQUIRED >0*
		timekeep_t burner_min_time;	///< minimum burner state time (i.e. minimum time spent in either on or off state). Prevents pumping. *Optional, defaults to 4mn*
		itid_t tid_boiler;		///< boiler temp id. *REQUIRED*
		itid_t tid_boiler_return;	///< boiler inflow temp id. *Required* if #limit_treturnmin is set
		orid_t rid_burner_1;		///< first stage of burner. *REQUIRED*
		orid_t rid_burner_2;		///< second stage of burner. *Optional* -- XXX NOT IMPLEMENTED
		struct {
			struct s_pump * restrict pump_load;	///< load pump for the boiler. *Optional*
			struct s_valve * restrict valve_ret;	///< mixing return valve for the boiler. *Optional*
		} p;		///< pointer-based settings. For configuration details see specific types instructions
	} set;		///< settings (externally set)
	struct {
		bool antifreeze;		///< true if anti freeze tripped
		_Atomic temp_t target_temp;	///< current target temp
		_Atomic temp_t actual_temp;	///< actual boiler temperature
		tempdiff_t turnon_negderiv;	///< value of negative derivative value at last turn on
		timekeep_t negderiv_starttime;	///< time at which a negative boiler temp derivative was first measured during burner on condition
		timekeep_t burner_1_last_switch;///< last time #set.rid_burner_1 was toggled
		uint32_t turnon_curr_adj;	///< computed value for current turn-on anticipation offset time
		uint32_t turnon_next_adj;	///< computed value for next turn-on anticipation offset time
		struct s_temp_intgrl boil_itg;	///< boiler integral (for cold start protection)
		struct s_temp_intgrl ret_itg;	///< return integral (for return temp management)
		struct s_temp_deriv temp_drv;	///< boiler temperature derivative
	} run;		///< private runtime (internally handled)
};

int boiler_heatsource(struct s_heatsource * const heat) __attribute__((warn_unused_result));

#endif /* boiler_h */
