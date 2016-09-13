//
//  rwchcd_plant.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_plant_h
#define rwchcd_plant_h

#include "rwchcd.h"

struct s_pump {
	bool configured;
	time_t set_cooldown_time;	///< preset cooldown time during which the pump remains on for transitions from on to off
	time_t actual_cooldown_time;	///< actual cooldown time remaining
	struct s_stateful_relay * restrict relay;
	char * restrict name;
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
struct s_valve {
	bool configured;
	temp_t deadzone;	///< valve deadzone: no operation when target in deadzone
	short position;		///< current position in %
	short target_position;	///< current target position
//	enum { MIXER, ZONE } type;	///< valve type, XXX probably not necessary, can be inferred
	short ete_time;		///< end-to-end run time
	enum { STOP, OPEN, CLOSE } action;
	char * restrict name;
	struct s_stateful_relay * restrict open;	///< relay for opening the valve
	struct s_stateful_relay * restrict close;	///< relay for closing the valve (if not set then spring return)
	tempid_t id_temp1;		///< temp at the "primary" input: when position is 0% there is 0% flow from this input
	tempid_t id_temp2;		///< temp at the "secondary" input: when position is 0% there is 100% flow from this input. if negative, offset in Celsius from temp1
	tempid_t id_tempout;	///< temp at the output
	short (*valvelaw)(const struct s_valve * const, temp_t);	///< pointer to valve law
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
//	enum { DIRECT, MIXED } type;	///< probably not necessary, can be inferred from valve type/presence
	struct s_valve * restrict valve;///< valve for circuit (if available, otherwise it's direct)
	struct s_pump * restrict pump;	///< pump for this circuit
//	temp_t histeresis;		///< histeresis for target temp
	temp_t set_limit_wtmin;		///< minimum water pipe temp when this circuit is active (e.g. for frost protection)
	temp_t set_limit_wtmax;		///< maximum allowed water pipe temp when this circuit is active
	temp_t set_tcomfort;		///< target ambient temp in comfort mode
	temp_t set_teco;		///< target ambient temp in eco mode
	temp_t set_tfrostfree;		///< target ambient temp in frost-free mode
	temp_t set_toffset;		///< global offset adjustment for ambient targets
	temp_t target_ambient;		///< current calculated ambient target temp
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
	struct s_templaw_data20C tlaw_data;
	temp_t (*templaw)(const struct s_heating_circuit * const, temp_t);	///< pointer to temperature law for this circuit, ref at 20C
	char * restrict name;		///< name for this circuit
};

struct s_boiler {
	bool configured;
	bool online;			///< true if boiler is available for use
	bool antifreeze;		///< true if anti freeze tripped
	//regime de coupure (p.48)
	temp_t histeresis;
	temp_t limit_tmax;		///< maximum boiler temp when operating
	temp_t limit_tmin;		///< minimum boiler temp when operating
	temp_t limit_treturnmin;	///< minimum boiler return temp (optional) -- XXX NOT IMPLEMENTED
	temp_t set_tfreeze;		///< trip point for antifreeze (+5C)
	time_t min_runtime;
	char * restrict name;
	struct s_pump * restrict loadpump;	///< load pump for the boiler, if present
	struct s_stateful_relay * restrict burner_1;	///< first stage of burner
	struct s_stateful_relay * restrict burner_2;	///< second stage of burner
	tempid_t id_temp;		///< burner temp id
	tempid_t id_temp_outgoing;	///< burner outflow temp id
	tempid_t id_temp_return;	///< burner inflow temp id
	temp_t target_temp;		///< current target temp
};

// XXX cascade
struct s_heat_source {
	bool configured;
	bool online;			///< true if source is available for use
	unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading -- XXX NOT IMPLEMENTED
	enum { BOILER, HOTTANK, FIXED } type;
	temp_t temp_request;		///< current temperature request for heat source (max of all requests)
	void * restrict source;		///< pointer to related heat source structure
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

struct s_heating_circuit_l {
	short id;
	struct s_heating_circuit * restrict circuit;
	struct s_heating_circuit_l * restrict next;
};

struct s_dhw_tank_l {
	short id;
	struct s_dhw_tank * restrict dhwt;
	struct s_dhw_tank_l * restrict next;
};

struct s_heat_source_l {
	short id;
	struct s_heat_source * restrict source;
	struct s_heat_source_l * restrict next;
};

struct s_plant {
	bool configured;
	unsigned short heat_source_n;	///< number of heat sources in the plant
	unsigned short heating_circuit_n;	///< number of heating circuits in the plant
	unsigned short dhw_tank_n;	///< number of dhw tanks in the plant
	struct s_heat_source_l * restrict heat_head;
	struct s_heating_circuit_l * restrict circuit_head;
	struct s_dhw_tank_l * restrict dhwt_head;
};

int set_pump_state(struct s_pump * const pump, bool state, bool force_state);
int get_pump_state(const struct s_pump * const pump);

#endif /* rwchcd_plant_h */
