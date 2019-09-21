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
#include "timekeep.h"
#include "lib.h"	// for s_temp_intgrl

/** heatsource type identifiers */
enum e_heatsource_type {
	HS_NONE = 0,		///< No heat source: that's a misconfiguration
	HS_BOILER,		///< boiler type heatsource. Config `boiler`
	HS_UNKNOWN,		///< invalid past this value
};

// XXX cascade
/** Heat source element structure */
struct s_heatsource {
	struct {
		bool configured;		///< true if properly configured
		schedid_t schedid;		///< schedule id for this heatsource.
		enum e_runmode runmode;		///< current heatsource set_runmode
		enum e_heatsource_type type;	///< type of heatsource
		unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading -- XXX NOT IMPLEMENTED
		timekeep_t consumer_sdelay;	///< if set, consumers will wait this much time before reducing their consumption (prevents heatsource overheating after e.g. burner run)
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if source is available for use (under software management)
		bool could_sleep;		///< true if source is could be sleeping (no recent heat request from circuits)
		bool overtemp;			///< true if heatsource is overtemp
		enum e_runmode runmode;		///< heatsource actual (computed) runmode
		temp_t temp_request;		///< current temperature request for heat source
		timekeep_t last_run_time;	///< last time heatsource was run
		timekeep_t target_consumer_sdelay;	///< calculated stop delay
		int_fast16_t cshift_crit;	///< critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		int_fast16_t cshift_noncrit;	///< non-critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		struct s_temp_intgrl sld_itg;	///< sliding priority integral, used to compute consummer shift when in DHW sliding priority
	} run;		///< private runtime (internally handled)
	const char * restrict name;		///< unique name for this heatsource
	const struct s_pdata * restrict pdata;	///< read-only plant data for this heatsource
	void * restrict priv;			///< pointer to source private data structure
	struct {
		int (*online)(struct s_heatsource * const);	///< pointer to source private online() function
		int (*offline)(struct s_heatsource * const);	///< pointer to source private offline() function
		int (*logic)(struct s_heatsource * const);	///< pointer to source private logic() function
		int (*run)(struct s_heatsource * const);	///< pointer to source private run() function
		temp_t (*temp)(struct s_heatsource * const);	///< pointer to source private temp() function (returns current temperature) @todo XXX only used in logic_heatsource()
		timekeep_t (*time)(struct s_heatsource * const);///< pointer to source private time() function (returns time of last temperature update) @todo XXX only used in logic_heatsource()
		void (*del_priv)(void * priv);			///< pointer to source private del() function
	} cb;		///< heatsource callbacks
};

struct s_heatsource * heatsource_new(void) __attribute__((warn_unused_result));
int heatsource_online(struct s_heatsource * const heat) __attribute__((warn_unused_result));
int heatsource_offline(struct s_heatsource * const heat);
int heatsource_run(struct s_heatsource * const heat) __attribute__((warn_unused_result));
void heatsource_del(struct s_heatsource * heat);

#endif /* heatsource_h */
