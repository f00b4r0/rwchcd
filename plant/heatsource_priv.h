//
//  plant/heatsource_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource internal API.
 */

#ifndef heatsource_priv_h
#define heatsource_priv_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "timekeep.h"
#include "scheduler.h"
#include "lib.h"	// for s_temp_intgrl

/** heatsource type identifiers */
enum e_heatsource_type {
	HS_NONE = 0,		///< No heat source: that's a misconfiguration
	HS_BOILER,		///< boiler type heatsource. Config `boiler`
	HS_UNKNOWN,		///< invalid past this value
};

/** Heat source element structure */
struct s_heatsource {
	struct {
		bool configured;		///< true if properly configured
		bool log;			///< true if data logging should be enabled for this heatsource. *Optional*
		schedid_t schedid;		///< schedule id for this heatsource. *Optional*
		enum e_runmode runmode;		///< current heatsource set_runmode. *REQUIRED*
		enum e_heatsource_type type;	///< type of heatsource. *REQUIRED*
		unsigned short prio;		///< priority: 0 (*default*) is highest prio, next positive, for cascading. *Optional* -- XXX NOT IMPLEMENTED
		timekeep_t consumer_sdelay;	///< if set, consumers will wait this much time before reducing their consumption (prevents heatsource overheating after e.g. burner run). *Optional*
	} set;		///< settings (externally set)
	struct {
		atomic_bool online;		///< true if source is available for use (under software management)
		atomic_bool could_sleep;	///< true if source is could be sleeping (no recent heat request from circuits)
		atomic_bool overtemp;		///< true if heatsource is overtemp
		_Atomic enum e_runmode runmode;	///< heatsource actual (computed) runmode
		_Atomic temp_t temp_request;	///< current temperature request for heat source
		timekeep_t last_run_time;	///< last time heatsource was run
		timekeep_t target_consumer_sdelay;	///< calculated stop delay
		int_least16_t cshift_crit;	///< critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		int_least16_t cshift_noncrit;	///< non-critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		struct s_temp_intgrl sld_itg;	///< sliding priority integral, used to compute consummer shift when in DHW sliding priority
	} run;		///< private runtime (internally handled)
	const char * restrict name;		///< unique name for this heatsource
	const struct s_pdata * pdata;		///< read-only plant data for this heatsource
	void * restrict priv;			///< pointer to source private data structure
	struct {
		int (*log_reg)(const struct s_heatsource * const);	///< pointer to source private log_register() function
		int (*log_dereg)(const struct s_heatsource * const);	///< pointer to source private log_deregister() function
		int (*online)(struct s_heatsource * const);	///< pointer to source private online() function
		int (*offline)(struct s_heatsource * const);	///< pointer to source private offline() function
		int (*logic)(struct s_heatsource * const);	///< pointer to source private logic() function. @note guaranteed to be called before .run()
		int (*run)(struct s_heatsource * const);	///< pointer to source private run() function
		temp_t (*temp)(struct s_heatsource * const);	///< pointer to source private temp() function (returns current temperature) @todo XXX only used in logic_heatsource()
		timekeep_t (*time)(struct s_heatsource * const);///< pointer to source private time() function (returns time of last temperature update) @todo XXX only used in logic_heatsource()
		void (*del_priv)(void * priv);			///< pointer to source private del() function
	} cb;		///< heatsource callbacks
	enum e_execs status;			///< last known status
};


#endif /* heatsource_priv_h */
