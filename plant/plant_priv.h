//
//  plant/plant_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant internal API.
 */

#ifndef plant_priv_h
#define plant_priv_h

#include <stdbool.h>

#include "timekeep.h"

#include "pump_priv.h"
#include "valve_priv.h"
#include "hcircuit.h"
#include "dhwt.h"
#include "heatsource.h"

typedef uint_fast8_t	plid_t;
#define PLID_MAX	UINT_FAST8_MAX

/**
 * Plant structure.
 * One plant is a coherent set of heatsource(s), circuit(s) and dhwt(s) all
 * connected to each other.
 */
struct s_plant {
	struct {
		bool configured;			///< true if properly configured
		bool summer_maintenance;		///< true if pumps/valves should be run periodically in summer. *Defaults to false*. See summer_run_interval and #summer_run_duration
		timekeep_t sleeping_delay;		///< if no circuit request for this much time, then plant could sleep (will trigger electric switchover when available). (*default*: 0 disables). *Optional*
		timekeep_t summer_run_interval;		///< interval between summer maintenance runs (suggested: 1 week). *Required* if #summer_maintenance is true
		timekeep_t summer_run_duration;		///< duration of summer maintenance operation (suggested: 10mn). *Required* if #summer_maintenance is true.
	} set;
	struct {
		bool online;			///< true if plant is online
		timekeep_t summer_timer;	///< timer for summer maintenance
		timekeep_t last_creqtime;	///< last recorded time for circuit heat request
		temp_t plant_hrequest;		///< plant heat request
		uint_fast8_t dhwt_maxprio;	///< largest online value for DHWT prio
	} run;
	struct s_pdata pdata;		///< plant-wide data shared with plant entities. No atomic/locking since this data is written/read within a single thread of execution (master)
	struct {
		struct s_pump * all;	///< pointer to dynamically allocated array of pumps, size n
		plid_t n;		///< number of allocated pumps
		plid_t last;		///< id of last free slot
	} pumps;		///< plant pumps
	struct {
		struct s_valve * all;	///< pointer to dynamically allocated array of valves, size n
		plid_t n;		///< number of allocated valves
		plid_t last;		///< id of last free slot
	} valves;		///< plant valves
	struct {
		struct s_hcircuit * all;///< pointer to dynamically allocated array of hcircuits, size n
		plid_t n;		///< number of allocated hcircuits
		plid_t last;		///< id of last free slot
	} hcircuits;		///< plant hcircuits
	struct {
		struct s_dhwt * all;	///< pointer to dynamically allocated array of dhwts, size n
		plid_t n;		///< number of allocated dhwts
		plid_t last;		///< id of last free slot
	} dhwts;		///< plant dhwts
	struct {
		struct s_heatsource * all;///< pointer to dynamically allocated array of heatsources, size n
		plid_t n;		///< number of allocated heatsources
		plid_t last;		///< id of last free slot
	} heatsources;		///< plant heats
};

#endif /* plant_priv_h */
