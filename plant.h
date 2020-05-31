//
//  plant.h
//  rwchcd
//
//  (C) 2016-2017 Thibaut VARENE
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

// https://www.lysator.liu.se/c/restrict.html#linked-lists

/** List of pumps */
struct s_pump_l {
	uint_fast8_t id;
	enum e_execs status;		///< pump actual status (this flag will signal the last run error).
	struct s_pump * restrict pump;
	struct s_pump_l * next;
};

/** List of valves */
struct s_valve_l {
	uint_fast8_t id;
	enum e_execs status;		///< valve actual status (this flag will signal the last run error)
	struct s_valve * restrict valve;
	struct s_valve_l * next;
};

/** List of heating circuits */
struct s_heating_circuit_l {
	uint_fast8_t id;
	enum e_execs status;		///< circuit actual status (this flag will signal the last run error)
	struct s_hcircuit * restrict circuit;
	struct s_heating_circuit_l * next;
};

/** List of DHWT */
struct s_dhw_tank_l {
	uint_fast8_t id;
	enum e_execs status;		///< dhwt actual status (this flag will signal the last run error)
	struct s_dhwt * restrict dhwt;
	struct s_dhw_tank_l * next;
};

/** List of heat sources */
struct s_heatsource_l {
	uint_fast8_t id;
	enum e_execs status;		///< heatsource actual status (this flag will signal the last run error)
	struct s_heatsource * restrict heats;
	struct s_heatsource_l * next;
};

/**
 * Plant structure.
 * One plant is a coherent set of heatsource(s), circuit(s) and dhwt(s) all
 * connected to each other.
 */
struct s_plant {
	bool configured;	///< true if properly configured
	struct {
		bool online;			///< true if plant is online
		timekeep_t summer_timer;	///< timer for summer maintenance
		timekeep_t last_creqtime;	///< last recorded time for circuit heat request
		temp_t plant_hrequest;		///< plant heat request
		uint_fast8_t dhwt_maxprio;	///< largest online value for DHWT prio
	} run;
	struct s_pdata pdata;
	uint_fast8_t pump_n;	///< number of pumps in the plant
	uint_fast8_t valve_n;	///< number of valves in the plant
	uint_fast8_t heats_n;	///< number of heat sources in the plant
	uint_fast8_t circuit_n;	///< number of heating circuits in the plant
	uint_fast8_t dhwt_n;	///< number of dhw tanks in the plant
	struct s_pump_l * restrict pump_head;	///< list of pumps in the plant
	struct s_valve_l * restrict valve_head;	///< list of valves in the plant
	struct s_heatsource_l * restrict heats_head;	///< list of heatsources in the plant
	struct s_heating_circuit_l * restrict circuit_head;	///< list of heating circuits in the plant
	struct s_dhw_tank_l * restrict dhwt_head;	///< list of DHWT in the plant
};

int plant_online(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
int plant_offline(struct s_plant * restrict const plant);
int plant_run(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
struct s_pump * plant_fbn_pump(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_pump * plant_new_pump(struct s_plant * restrict const plant, const char * restrict const name);
struct s_valve * plant_fbn_valve(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_valve * plant_new_valve(struct s_plant * restrict const plant, const char * restrict const name);
struct s_hcircuit * plant_fbn_circuit(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_hcircuit * plant_new_circuit(struct s_plant * restrict const plant, const char * restrict const name);
struct s_dhwt * plant_fbn_dhwt(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_dhwt * plant_new_dhwt(struct s_plant * restrict const plant, const char * restrict const name);
struct s_heatsource * plant_fbn_heatsource(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_heatsource * plant_new_heatsource(struct s_plant * restrict const plant, const char * restrict const name);
struct s_plant * plant_new(void);
void plant_del(struct s_plant * plant);

#endif /* rwchcd_plant_h */
