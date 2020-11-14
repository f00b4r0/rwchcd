//
//  plant/plant.h
//  rwchcd
//
//  (C) 2016-2017,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation API.
 */

#ifndef rwchcd_plant_h
#define rwchcd_plant_h

#include "rwchcd.h"
#include "timekeep.h"

#include "pump.h"
#include "valve.h"
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
		bool summer_maintenance;		///< true if pumps/valves should be run periodically in summer. See #summer_run_interval and #summer_run_duration
		timekeep_t sleeping_delay;		///< if no circuit request for this much time, then plant could sleep (will trigger electric switchover when available)
		timekeep_t summer_run_interval;		///< interval between summer maintenance runs (suggested: 1 week). @note if #summer_maintenance is true then this MUST be set
		timekeep_t summer_run_duration;		///< duration of summer maintenance operation (suggested: 10mn). @note if #summer_maintenance is true then this MUST be set
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

int plant_online(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
int plant_offline(struct s_plant * restrict const plant);
int plant_run(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
struct s_pump * plant_fbn_pump(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_valve * plant_fbn_valve(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_hcircuit * plant_fbn_hcircuit(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_dhwt * plant_fbn_dhwt(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_heatsource * plant_fbn_heatsource(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_plant * plant_new(void);
void plant_del(struct s_plant * plant);

#endif /* rwchcd_plant_h */
