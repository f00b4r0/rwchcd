//
//  rwchcd_plant.h
//  
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

#include <time.h>
#include "rwchcd.h"

#define RWCHCD_TEMP_NOREQUEST	0

#define RWCHCD_CSHIFT_MAX	200		///< Maximum value for consumer shift

/** Pump element structure */
struct s_pump {
	struct {
		bool configured;		///< true if properly configured
		time_t cooldown_time;		///< preset cooldown time during which the pump remains on for transitions from on to off - useful to prevent short runs that might clog the pump
		relid_t rid_relay;		///< hardware relay controlling that pump
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if pump is operational (under software management)
		time_t actual_cooldown_time;	///< actual cooldown time remaining
		bool req_on;			///< request pump on
		bool force_state;		///< true if req_state should be forced (no cooldown)
	} run;		///< private runtime (internally handled)
	char * restrict name;
};

struct s_valve_sapprox_priv {
	uint_fast8_t set_amount;	///< amount to move in %
	time_t set_sample_intvl;	///< sample interval in seconds
	time_t last_time;		///< private variable
};

// http://wiki.diyfaq.org.uk/index.php?title=Motorised_Valves
/** Valve element structure */
struct s_valve {
	struct {
		bool configured;	///< true if properly configured
		temp_t tdeadzone;	///< valve deadzone: no operation when target temp in deadzone
		uint_fast8_t deadband;	///< deadband for valve operation in %: no operation if requested move is less than that
		time_t ete_time;	///< end-to-end run time in seconds
		tempid_t id_temp1;	///< temp at the "primary" input: when position is 0% there is 0% flow from this input
		tempid_t id_temp2;	///< temp at the "secondary" input: when position is 0% there is 100% flow from this input. if negative, offset in Kelvin from temp1
		tempid_t id_tempout;	///< temp at the output
		relid_t rid_open;	///< relay for opening the valve
		relid_t rid_close;	///< relay for closing the valve
	} set;		///< settings (externally set)
	struct {
		bool online;		///< true if valve is operational (under software management)
		bool in_deadzone;	///< true if valve is in deadzone (XXX USEFUL?)
		bool true_pos;		///< true if estimated position is "true": position measured from a full close/open start
		int_fast16_t actual_position;	///< estimated current position in %*10
		uint_fast16_t target_course;	///< current target course in % of set.ete_time
		time_t running_since;	///< current operation (OPEN/CLOSE) start time
		time_t acc_open_time;	///< accumulated open time since last close
		time_t acc_close_time;	///< accumulated close time since last open
		enum { STOP = 0, OPEN, CLOSE } actual_action,	///< current valve action
						request_action;	///< requested action
	} run;		///< private runtime (internally handled)
	char * restrict name;
	void * restrict priv;	///< private data structure for valvelaw
	int (*valvelaw)(struct s_valve * restrict const, const temp_t);	///< pointer to valve law
};

/** private data for templaw_bilinear (for 20C ambient target) */
struct s_tlaw_bilin20C_priv {
	temp_t tout1;		///< outside temp1 (lowest outdoor temp)
	temp_t twater1;		///< corresponding target water temp1 (highest water temp)
	temp_t tout2;		///< outside temp2 (highest outdoor temp)
	temp_t twater2;		///< corresponding target water temp2 (lowest water temp)
	int_fast16_t nH100;	///< thermal non-linearity coef *100 (e.g. if nH is 1.3, nH100 is 130)
};

/** Heating circuit element structure */
struct s_heating_circuit {
	struct {
		bool configured;		///< true if circuit is configured
		enum e_runmode runmode;		///< current circuit set_runmode
		struct s_circuit_params params;	///< local parameters overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max.
		short ambient_factor;		///< influence of ambient temp on templaw calculations, in percent - XXX REVIEW TYPE
		temp_t wtemp_rorh;		///< water temp rate of rise in temp per hour
		bool fast_cooldown;		///< if true, switching to cooler mode triggers active cooldown (heating is disabled until temperature has cooled to new target)
		time_t am_tambient_tK;		///< ambient model: time necessary for 1 Kelvin temperature rise (seconds)
		temp_t tambient_boostdelta;	///< temperature delta applied during boost turn-on
		time_t max_boost_time;		///< maximum duration of transition boost
		tempid_t id_temp_outgoing;	///< outgoing temp sensor for this circuit
		tempid_t id_temp_return;	///< return temp sensor for this circuit
		tempid_t id_temp_ambient;	///< ambient temp sensor related to this circuit
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if circuit is operational (under software management)
		bool outhoff;			///< true if no heating conditions are met
		time_t last_run_time;		///< last time circuit_run() was invoked
		enum e_runmode runmode;		///< circuit actual (computed) runmode
		temp_t rorh_last_target;	///< previous set point target for rorh control
		time_t rorh_update_time;	///< last time output was updated with respect to rorh
		time_t actual_cooldown_time;	///< actual turn off cooldown time remaining
		temp_t request_ambient;		///< current requested ambient target temp
		temp_t target_ambient;		///< current calculated ambient target temp (includes offset and computed shifts)
		enum { TRANS_NONE = 0, TRANS_UP, TRANS_DOWN } transition;	///< current transition underwent by the circuit
		time_t trans_since;		///< transition start time
		temp_t trans_start_temp;	///< temperature at transition start
		temp_t actual_ambient;		///< actual ambient temperature (either from sensor, or modelled)
		temp_t target_wtemp;		///< current target water temp
		temp_t heat_request;		///< current temp request from heat source for this circuit
	} run;		///< private runtime (internally handled)
	temp_t (*templaw)(const struct s_heating_circuit * restrict const, temp_t);	///< pointer to temperature law for this circuit, ref at 20C
	void * restrict tlaw_data_priv;		///< Reference data for templaw
	struct s_valve * restrict valve;	///< valve for circuit (if available, otherwise it's direct)
	struct s_pump * restrict pump;		///< pump for this circuit
	char * restrict name;			///< name for this circuit
};

/** Boiler heatsource private structure */
// XXX TODO: return mixing valve / isolation valve / modulating burner
struct s_boiler_priv {
	struct {
		enum {
			IDLE_NEVER = 0,		///< boiler runs always at least at limit_tmin
			IDLE_FROSTONLY,		///< boiler turns off only in frost free
			IDLE_ALWAYS,		///< boiler turns off any time there's no heat request
		} idle_mode;		///< boiler off regime (p.48)
		temp_t histeresis;		///< boiler temp histeresis
		temp_t limit_thardmax;		///< "safety" trip temperature. Past this temperature the boiler will (be stopped and) require consumers to maximize their usage to dissipate heat faster.
		temp_t limit_tmax;		///< maximum boiler temp when operating. Must be < limit_thardmax
		temp_t limit_tmin;		///< minimum boiler temp when operating
		temp_t limit_treturnmin;	///< minimum boiler return temp (optional) -- XXX NOT IMPLEMENTED
		temp_t t_freeze;		///< boiler temp trip point for antifreeze (+5C)
		time_t burner_min_time;		///< minimum burner state time (i.e. minimum time spent in either on or off state). Prevents pumping
		tempid_t id_temp;		///< boiler temp id
		tempid_t id_temp_outgoing;	///< boiler outflow temp id
		tempid_t id_temp_return;	///< boiler inflow temp id
		relid_t rid_burner_1;		///< first stage of burner
		relid_t rid_burner_2;		///< second stage of burner
	} set;		///< settings (externally set)
	struct {
		bool antifreeze;		///< true if anti freeze tripped
		temp_t target_temp;		///< current target temp
	} run;		///< private runtime (internally handled)
	struct s_pump * restrict loadpump;	///< load pump for the boiler, if present
};

enum e_heatsource_type {
	NONE = 0,	///< No heat source (XXX should probably be an error)
	BOILER,		///< boiler type heatsource
};

// XXX cascade
/** Heat source element structure */
struct s_heatsource {
	struct {
		bool configured;		///< true if properly configured
		enum e_runmode runmode;		///< current heatsource set_runmode
		enum e_heatsource_type type;	///< type of heatsource
		unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading -- XXX NOT IMPLEMENTED
		time_t sleeping_time;		///< if no request for this much time, then mark heat source as can sleep
		time_t consumer_stop_delay;	///< if set, consumers will wait this much time before reducing their consumption (prevents heatsource overheating after e.g. burner run)
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if source is available for use (under software management)
		bool could_sleep;		///< true if source is could be sleeping (no recent heat request from circuits)
		enum e_runmode runmode;		///< heatsource actual (computed) runmode
		temp_t temp_request;		///< current temperature request for heat source (max of all requests)
		time_t last_circuit_reqtime;	///< last time a circuit has put out a request for that heat source
		time_t target_consumer_stop_delay;	///< calculated stop delay
		int_fast16_t consumer_shift;	///< factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
	} run;		///< private runtime (internally handled)
	char * restrict name;
	void * restrict priv;			///< pointer to source private data structure
	int (*hs_online)(struct s_heatsource * const);	///< pointer to source private online() function
	int (*hs_offline)(struct s_heatsource * const);	///< pointer to source private offline() function
	int (*hs_logic)(struct s_heatsource * const);	///< pointer to source private logic() function
	int (*hs_run)(struct s_heatsource * const);	///< pointer to source private run() function
	void (*hs_del_priv)(void * priv);		///< pointer to source private del() function
};

struct s_solar_heater {
	struct {
		bool configured;
		tempid_t id_temp_panel;		///< current panel temp for this circuit
	} set;		///< settings (externally set)
	struct s_pump * restrict pump;		///< pump for this circuit
	char * restrict name;
};


/** DHWT element structure */
struct s_dhw_tank {
	struct {
		bool configured;		///< true if properly configured
		unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading - XXX NOT IMPLEMENTED
		enum e_runmode runmode;		///< dhwt set_runmode
		enum { DHWTP_ABSOLUTE, DHWTP_SLIDDHW, DHWTP_SLIDMAX, DHWTP_PARALDHW, DHWTP_PARALMAX };	///< XXX priorite ECS - absolute, glissante (ecs ou max), aucune (parallele ecs ou max)
		enum {
			DHWTF_NEVER = 0,	///< never force
			DHWTF_FIRST,		///< force first comfort charge of the day
			DHWTF_ALWAYS		///< force all comfort charges
		} force_mode;			///< Programmed force charge when switching to COMFORT
		tempid_t id_temp_bottom;	///< temp sensor at bottom of dhw tank
		tempid_t id_temp_top;		///< temp sensor at top of dhw tank
		tempid_t id_temp_win;		///< temp sensor heatwater inlet
		tempid_t id_temp_wout;		///< temp sensor heatwater outlet
		relid_t rid_selfheater;		///< relay for internal electric heater (if available)
		struct s_dhwt_params params;	///< local parameter overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max.
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if tank is available for use (under software management)
		bool charge_on;			///< true if charge ongoing
		bool recycle_on;		///< true if recycle pump should be running
		bool force_on;			///< true if charge should be forced even if current temp is above the charge threshold (but below the target)
		bool legionella_on;		///< true if anti-legionella charge is required
		bool charge_overtime;		///< true if charge went overtime
		enum e_runmode runmode;		///< dhwt actual (computed) runmode
		temp_t target_temp;		///< current target temp for this tank
		temp_t heat_request;		///< current temp request from heat source for this circuit
		time_t mode_since;		///< starting time of current mode (if charge_on: charge start time, else charge end time)
		int charge_yday;		///< last day forced charge was triggered in DHWTF_FIRST mode
	} run;		///< private runtime (internally handled)
	struct s_solar_heater * restrict solar;	///< solar heater (if avalaible) - XXX NOT IMPLEMENTED
	struct s_pump * restrict feedpump;	///< feed pump for this tank
	struct s_pump * restrict recyclepump;	///< dhw recycle pump for this tank
	char * restrict name;			///< name for this tank
};

/** List of pumps */
struct s_pump_l {
	uint_fast8_t id;
	enum e_execs status;		///< pump actual status (this flag will signal the last run error). @note UNUSED
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
	struct s_heating_circuit * restrict circuit;
	struct s_heating_circuit_l * next;
};

/** List of DHWT */
struct s_dhw_tank_l {
	uint_fast8_t id;
	enum e_execs status;		///< dhwt actual status (this flag will signal the last run error)
	struct s_dhw_tank * restrict dhwt;
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
struct s_pump * plant_new_pump(struct s_plant * const plant);
struct s_valve * plant_new_valve(struct s_plant * const plant);
struct s_heating_circuit * plant_new_circuit(struct s_plant * const plant);
struct s_dhw_tank * plant_new_dhwt(struct s_plant * const plant);
struct s_heatsource * plant_new_heatsource(struct s_plant * const plant, enum e_heatsource_type type);
struct s_plant * plant_new(void);
void plant_del(struct s_plant * plant);
int circuit_make_bilinear(struct s_heating_circuit * const circuit);
int valve_make_bangbang(struct s_valve * const valve);
int valve_make_sapprox(struct s_valve * const valve);

#endif /* rwchcd_plant_h */
