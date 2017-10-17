//
//  heatsource.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource operation API.
 */

#ifndef heatsource_h
#define heatsource_h

#include "rwchcd.h"

/** Boiler heatsource private structure. @todo XXX TODO: return mixing valve / isolation valve / modulating burner */
struct s_boiler_priv {
	struct {
		enum {
			IDLE_NEVER = 0,		///< boiler runs always at least at limit_tmin
			IDLE_FROSTONLY,		///< boiler turns off only in frost free
			IDLE_ALWAYS,		///< boiler turns off any time there's no heat request
		} idle_mode;		///< boiler off regime
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
		struct s_temp_intgrl boil_itg;	///< boiler integral (for cold start protection)
	} run;		///< private runtime (internally handled)
	struct s_pump * restrict loadpump;	///< load pump for the boiler, if present
	struct s_valve * restrict retvalve;	///< return valve for the boiler, if present -- XXX NOT IMPLEMENTED
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
		time_t consumer_sdelay;		///< if set, consumers will wait this much time before reducing their consumption (prevents heatsource overheating after e.g. burner run)
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if source is available for use (under software management)
		bool could_sleep;		///< true if source is could be sleeping (no recent heat request from circuits)
		enum e_runmode runmode;		///< heatsource actual (computed) runmode
		temp_t temp_request;		///< current temperature request for heat source (max of all requests)
		time_t last_run_time;		///< last time heatsource was run
		time_t last_circuit_reqtime;	///< last time a circuit has put out a request for that heat source
		time_t target_consumer_sdelay;	///< calculated stop delay
		int_fast16_t cshift_crit;	///< critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		int_fast16_t cshift_noncrit;	///< non-critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		struct s_temp_intgrl sld_itg;	///< sliding priority integral, used to compute consummer shift when in DHW sliding priority
	} run;		///< private runtime (internally handled)
	char * restrict name;
	void * restrict priv;			///< pointer to source private data structure
	int (*hs_online)(struct s_heatsource * const);	///< pointer to source private online() function
	int (*hs_offline)(struct s_heatsource * const);	///< pointer to source private offline() function
	int (*hs_logic)(struct s_heatsource * const);	///< pointer to source private logic() function
	int (*hs_run)(struct s_heatsource * const);	///< pointer to source private run() function
	temp_t (*hs_temp_out)(struct s_heatsource * const);	///< pointer to source private temp_out() function (returns current output temperature)
	void (*hs_del_priv)(void * priv);		///< pointer to source private del() function
};

int heatsource_online(struct s_heatsource * const heat);
int heatsource_offline(struct s_heatsource * const heat);
int heatsource_run(struct s_heatsource * const heat);
void heatsource_del(struct s_heatsource * heat);

int heatsource_make_boiler(struct s_heatsource * const heat);

#endif /* heatsource_h */
