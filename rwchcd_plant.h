//
//  rwchcd_plant.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_plant_h
#define rwchcd_plant_h


struct s_pump {
	bool configured;
	time_t cooldown_time;
	struct s_stateful_relay relay;
	char * name;
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
	char * name;
	struct s_stateful_relay * open;	///< relay for opening the valve
	struct s_stateful_relay * close;	///< relay for closing the valve (if not set then spring return)
	tempid_t id_temp1;		///< temp at the "primary" input: when position is 0% there is 0% flow from this input
	tempid_t id_temp2;		///< temp at the "secondary" input: when position is 0% there is 100% flow from this input. if negative, offset in Celsius from temp1
	tempid_t id_tempout;	///< temp at the output
	short (*valvelaw)(const struct * const s_valve, temp_t);	///< pointer to valve law
};

struct s_templaw_data {
	temp_t tout1;		///< outside temp1
	temp_t twater1;		///< corresponding target water temp1
	temp_t tout2;		///< outside temp2
	temp_t twater2;		///< corresponding target water temp2
};

struct s_heating_circuit {
	bool configured;
	bool online;			///< true if circuit is operational
	enum e_runmode runmode;
//	enum { DIRECT, MIXED } type;	///< probably not necessary, can be inferred from valve type/presence
	struct s_valve * valve;		///< valve for circuit (if available, otherwise it's direct)
	struct s_pump * pump;		///< pump for this circuit
//	temp_t histeresis;		///< histeresis for target temp
	temp_t limit_wtmin;		///< minimum water pipe temp when this circuit is active (e.g. for frost protection)
	temp_t limit_wtmax;		///< maximum allowed water pipe temp when this circuit is active
	temp_t target_tcomfort;		///< target temp in comfort mode
	temp_t target_teco;		///< target temp in eco mode
	temp_t target_tfrostfree;	///< target temp in frost-free mode
	temp_t target_toffset;		///< offset adjustment for targets
	tempid_t id_temp_outgoing;	///< current temp for this circuit
	tempid_t id_temp_return;	///< current return temp for this circuit
	tempid_t id_temp_ambient;	///< ambient temp related to this circuit
	temp_t target_wtemp;		///< current target water temp
	temp_t temp_inoffset;		///< offset temp for heat source request
	temp_t heat_request;		///< current temp request from heat source for this circuit
	struct s_templaw_data tlaw_data;
	temp_t (*templaw)(const struct * const s_heating_circuit, temp_t);	///< pointer to temperature law for this circuit
	char * name;			///< name for this circuit
};

struct s_boiler {
	bool configured;
	bool online;			///< true if boiler is available for use
	temp_t histeresis;
	temp_t limit_tmax;
	temp_t limit_tmin;
	time_t min_runtime;
	char * name;
	struct s_pump * loadpump;	///< load pump for the boiler, if present
	struct s_stateful_relay * burner_1;	///< first stage of burner
	struct s_stateful_relay * burner_2;	///< second stage of burner
	tempid_t id_temp;		///< burner temp id
	tempid_t id_temp_outgoing;	///< burner outflow temp id
	tempid_t id_temp_return;	///< burner inflow temp id
	temp_t target_temp;		///< current target temp
};

// XXX cascade
struct s_heat_source {
	bool configured;
	bool online;			///< true if source is available for use
	unsigned short prio;		///< priority: 0 is highest prio, next positive
	enum { BOILER, HOTTANK, FIXED } type;
	temp_t temp_request;		///< current temperature request for heat source (max of all requests)
	void * source;			///< pointer to related heat source structure
};

struct s_solar_heater {
	bool configured;
	struct s_pump * pump;		///< pump for this circuit
	tempid_t id_temp_panel;		///< current panel temp for this circuit
	char * name;
};

struct s_dhw_tank {
	bool configured;
	bool online;			///< true if tank is available for use
	bool recycle_on;		///< true if recycle pump should be running
	bool heating_on;		///< true if a heating cycle is in progress
	unsigned short prio;		///< priority: 0 is highest prio, next positive - XXX NOT IMPLEMENTED
	enum e_runmode runmode;
	struct s_solar_heater * solar;	///< solar heater (if avalaible) - XXX NOT IMPLEMENTED
	struct s_pump * feedpump;	///< feed pump for this tank
	struct s_pump * recyclepump;	///< dhw recycle pump for this tank
	struct s_stateful_relay * selfheater;	///< relay for internal electric heater (if available)
	tempid_t id_temp_bottom;	///< temp at bottom of dhw tank
	tempid_t id_temp_top;		///< temp at top of dhw tank
	temp_t limit_wintmin;		///< minimum water intake temp when active
	temp_t limit_wintmax;		///< maximum allowed water intake temp when active
	temp_t limit_tmin;		///< minimum dhwt temp when active (e.g. for frost protection)
	temp_t limit_tmax;		///< maximum allowed dhwt temp when active
	temp_t target_tcomfort;		///< target temp in comfort mode
	temp_t target_teco;		///< target temp in eco mode
	temp_t target_tfrostfree;	///< target temp in frost-free mode
	temp_t target_temp;		///< current target temp for this tank
	temp_t histeresis;		///< histeresis for target temp
	temp_t temp_inoffset;		///< offset temp for heat source request
	temp_t heat_request;		///< current temp request from heat source for this circuit
	char * name;			///< name for this tank
};

struct s_heating_circuit_l {
	short id;
	struct s_heating_circuit * circuit;
	struct s_heating_circuit_l * next;
};

struct s_dhw_tank_l {
	short id;
	struct s_dhw_tank * dhwt;
	struct s_dhw_tank_l * next;
};

struct s_heat_source_l {
	short id;
	struct s_heat_source * source;
	struct s_heat_source_l * next;
};

struct s_plant {
	bool configured;
	unsigned short heat_source_n;	///< number of heat sources in the plant
	unsigned short heating_circuit_n;	///< number of heating circuits in the plant
	unsigned short dhw_tank_n;	///< number of dhw tanks in the plant
	struct s_heat_source_l * heat_head;
	struct s_mixed_circuit_l * circuit_head;
	struct s_dhw_tank_l * dhwt_head;
};

#endif /* rwchcd_plant_h */
