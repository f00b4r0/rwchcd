//
//  rwchcd_plant.h
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_plant_h
#define rwchcd_plant_h

#include <time.h>
#include "rwchcd.h"

struct s_pump {
	bool configured;
	bool online;			///< true if pump is operational
	time_t set_cooldown_time;	///< preset cooldown time during which the pump remains on for transitions from on to off
	time_t actual_cooldown_time;	///< actual cooldown time remaining
	struct s_stateful_relay * restrict relay;
	char * restrict name;
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
struct s_valve {
	bool configured;
	bool online;		///< true if valve is operational
	bool in_deadzone;	///< true if valve is in deadzone
	temp_t set_tdeadzone;	///< valve deadzone: no operation when target temp in deadzone
	int_fast8_t set_deadband;	///< deadband for valve operation in %
	int_fast8_t actual_position;	///< current position in %
	int_fast8_t last_rest_position;	///< last known resting position ("valve last stopped at")
	int_fast8_t target_position;	///< current target position in %
	int_fast8_t target_course;	///< current target course in % of set_ete_time (positive means open, negative means close)
	int_fast16_t set_ete_time;	///< end-to-end run time in seconds
	time_t stop_time;
	enum { STOP = 0, OPEN, CLOSE } action, request_action;	///< current valve action
	struct s_stateful_relay * restrict open;	///< relay for opening the valve
	struct s_stateful_relay * restrict close;	///< relay for closing the valve (if not set then spring return)
	tempid_t id_temp1;		///< temp at the "primary" input: when position is 0% there is 0% flow from this input
	tempid_t id_temp2;		///< temp at the "secondary" input: when position is 0% there is 100% flow from this input. if negative, offset in Kelvin from temp1
	tempid_t id_tempout;	///< temp at the output
	char * restrict name;
	int (*valvelaw)(struct s_valve * const, const temp_t);	///< pointer to valve law
};

struct s_templaw_data20C {
	temp_t tout1;		///< outside temp1
	temp_t twater1;		///< corresponding target water temp1
	temp_t tout2;		///< outside temp2
	temp_t twater2;		///< corresponding target water temp2
};

struct s_heating_circuit {
	bool configured;		///< true if circuit is configured
	bool online;			///< true if circuit is operational
	bool outhoff;			///< true if no heating conditions are met
	enum e_runmode set_runmode;	///< current circuit set_runmode
	enum e_runmode actual_runmode;	///< circuit actual (computed) runmode
	struct s_valve * restrict valve;///< valve for circuit (if available, otherwise it's direct)
	struct s_pump * restrict pump;	///< pump for this circuit
	temp_t limit_wtmin;		///< minimum water pipe temp when this circuit is active (e.g. for frost protection)
	temp_t limit_wtmax;		///< maximum allowed water pipe temp when this circuit is active
	temp_t set_tcomfort;		///< target ambient temp in comfort mode
	temp_t set_teco;		///< target ambient temp in eco mode
	temp_t set_tfrostfree;		///< target ambient temp in frost-free mode
	temp_t set_toffset;		///< global offset adjustment for ambient targets
	temp_t request_ambient;		///< current requested ambient target temp
	temp_t target_ambient;		///< current calculated ambient target temp (includes offset and computed shifts)
	temp_t set_outhoff_comfort;	///< outdoor temp for no heating in comfort mode
	temp_t set_outhoff_eco;		///< outdoor temp for no heating in eco mode
	temp_t set_outhoff_frostfree;	///< outdoor temp for no heating in frostfree mode
	temp_t set_outhoff_histeresis;	///< histeresis for no heating condition
	tempid_t id_temp_outgoing;	///< current temp for this circuit
	tempid_t id_temp_return;	///< current return temp for this circuit
	tempid_t id_temp_ambient;	///< ambient temp related to this circuit
	short set_ambient_factor;	///< influence of ambient temp on templaw calculations, in percent
	temp_t set_wtemp_rorh;		///< water temp rate of rise in temp per hour -- XXX NOT IMPLEMENTED
	temp_t target_wtemp;		///< current target water temp
	temp_t set_temp_inoffset;	///< offset temp for heat source request
	temp_t heat_request;		///< current temp request from heat source for this circuit
	struct s_templaw_data20C tlaw_data;	///< Reference data for templaw (for 20C ambient target)
	temp_t (*templaw)(const struct s_heating_circuit * const, temp_t);	///< pointer to temperature law for this circuit, ref at 20C
	char * restrict name;		///< name for this circuit
};

struct s_boiler_priv {
	bool antifreeze;		///< true if anti freeze tripped
	//regime de coupure (p.48)
	temp_t histeresis;		///< boiler temp histeresis
	temp_t limit_tmax;		///< maximum boiler temp when operating
	temp_t limit_tmin;		///< minimum boiler temp when operating
	temp_t limit_treturnmin;	///< minimum boiler return temp (optional) -- XXX NOT IMPLEMENTED
	temp_t set_tfreeze;		///< trip point for antifreeze (+5C)
	time_t set_burner_min_time;	///< minimum burner runtime and cooldown time
	time_t set_sleeping_time;	///< if no request for this much time, then put boiler asleep
	struct s_pump * restrict loadpump;	///< load pump for the boiler, if present
	struct s_stateful_relay * restrict burner_1;	///< first stage of burner
	struct s_stateful_relay * restrict burner_2;	///< second stage of burner
	tempid_t id_temp;		///< burner temp id
	tempid_t id_temp_outgoing;	///< burner outflow temp id
	tempid_t id_temp_return;	///< burner inflow temp id
	temp_t target_temp;		///< current target temp
};

enum e_heatsource_type {
	NONE = 0,	///< No heat source (XXX should probably be an error)
	BOILER,		///< boiler type heatsource
};

// XXX cascade
struct s_heatsource {
	bool configured;
	bool online;			///< true if source is available for use
	bool sleeping;			///< true if source is asleep
	enum e_runmode set_runmode;	///< current heatsource set_runmode
	enum e_runmode actual_runmode;	///< heatsource actual (computed) runmode
	enum e_heatsource_type type;	///< type of heatsource
	unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading -- XXX NOT IMPLEMENTED
	temp_t temp_request;		///< current temperature request for heat source (max of all requests)
	char * restrict name;
	void * restrict priv;		///< pointer to source private data structure
	int (*hs_online)(struct s_heatsource * const);	///< pointer to source private online() function
	int (*hs_offline)(struct s_heatsource * const);	///< pointer to source private offline() function
	int (*hs_run)(struct s_heatsource * const);	///< pointer to source private run() function
	void (*hs_del_priv)(void * priv);		///< pointer to source private del() function
};

struct s_solar_heater {
	bool configured;
	struct s_pump * restrict pump;	///< pump for this circuit
	tempid_t id_temp_panel;		///< current panel temp for this circuit
	char * restrict name;
};

struct s_dhw_tank {
	bool configured;
	bool online;			///< true if tank is available for use
	bool recycle_on;		///< true if recycle pump should be running
	bool force_on;			///< true if charge should be forced even if current temp is above the charge threshold (but below the target)
	bool charge_on;			///< true if a charge cycle is in progress
	unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading - XXX NOT IMPLEMENTED
	enum e_runmode set_runmode;	///< dhwt set_runmode
	enum e_runmode actual_runmode;	///< dhwt actual (computed) runmode
	enum { DHWTP_ABSOLUTE, DHWTP_SLIDDHW, DHWTP_SLIDMAX, DHWTP_PARALDHW, DHWTP_PARALMAX };	///< XXX priorite ECS - absolute, glissante (ecs ou max), aucune (parallele ecs ou max)
	struct s_solar_heater * restrict solar;	///< solar heater (if avalaible) - XXX NOT IMPLEMENTED
	struct s_pump * restrict feedpump;	///< feed pump for this tank
	struct s_pump * restrict recyclepump;	///< dhw recycle pump for this tank
	struct s_stateful_relay * restrict selfheater;	///< relay for internal electric heater (if available)
	time_t limit_chargetime;	///< maximum duration of charge time -- XXX NOT IMPLEMENTED p67
	tempid_t id_temp_bottom;	///< temp sensor at bottom of dhw tank
	tempid_t id_temp_top;		///< temp sensor at top of dhw tank
	tempid_t id_temp_win;		///< temp sensor heatwater inlet
	tempid_t id_temp_wout;		///< temp sensor heatwater outlet
	temp_t limit_wintmin;		///< minimum water intake temp when active
	temp_t limit_wintmax;		///< maximum allowed water intake temp when active
	temp_t limit_tmin;		///< minimum dhwt temp when active (e.g. for frost protection)
	temp_t limit_tmax;		///< maximum allowed dhwt temp when active
	temp_t set_tlegionella;		///< target temp for legionella prevention - XXX NOT IMPLEMENTED
	temp_t set_tcomfort;		///< target temp in comfort mode - XXX setup ensure > tfrostfree
	temp_t set_teco;		///< target temp in eco mode - XXX setup ensure > tfrostfree
	temp_t set_tfrostfree;		///< target temp in frost-free mode - XXX setup ensure > 0C
	temp_t target_temp;		///< current target temp for this tank
	temp_t histeresis;		///< histeresis for target temp - XXX setup ensure > 0C
	temp_t set_temp_inoffset;	///< offset temp for heat source request - XXX setup ensure > 0C
	temp_t heat_request;		///< current temp request from heat source for this circuit
	char * restrict name;		///< name for this tank
};

struct s_pump_l {
	uint_fast8_t id;
	struct s_pump * restrict pump;
	struct s_pump_l * restrict next;
};

struct s_valve_l {
	uint_fast8_t id;
	struct s_valve * restrict valve;
	struct s_valve_l * restrict next;
};

struct s_heating_circuit_l {
	uint_fast8_t id;
	struct s_heating_circuit * restrict circuit;
	struct s_heating_circuit_l * restrict next;
};

struct s_dhw_tank_l {
	uint_fast8_t id;
	struct s_dhw_tank * restrict dhwt;
	struct s_dhw_tank_l * restrict next;
};

struct s_heatsource_l {
	uint_fast8_t id;
	struct s_heatsource * restrict heats;
	struct s_heatsource_l * restrict next;
};

struct s_plant {
	bool configured;
	uint_fast8_t pump_n;	///< number of pumps in the plant
	uint_fast8_t valve_n;	///< number of valves in the plant
	uint_fast8_t heats_n;		///< number of heat sources in the plant
	uint_fast8_t circuit_n;	///< number of heating circuits in the plant
	uint_fast8_t dhwt_n;		///< number of dhw tanks in the plant
	struct s_pump_l * restrict pump_head;
	struct s_valve_l * restrict valve_head;
	struct s_heatsource_l * restrict heats_head;
	struct s_heating_circuit_l * restrict circuit_head;
	struct s_dhw_tank_l * restrict dhwt_head;
};

int plant_online(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
int plant_run(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
struct s_pump * plant_new_pump(struct s_plant * const plant);
struct s_valve * plant_new_valve(struct s_plant * const plant);
struct s_heating_circuit * plant_new_circuit(struct s_plant * const plant);
struct s_dhw_tank * plant_new_dhwt(struct s_plant * const plant);
struct s_heatsource * plant_new_heatsource(struct s_plant * const plant, enum e_heatsource_type type);
struct s_plant * plant_new(void);
void plant_del(struct s_plant * plant);
int circuit_make_linear(struct s_heating_circuit * const circuit);
int valve_make_linear(struct s_valve * const valve);
int valve_make_bangbang(struct s_valve * const valve);
int valve_make_sapprox(struct s_valve * const valve);

#endif /* rwchcd_plant_h */
