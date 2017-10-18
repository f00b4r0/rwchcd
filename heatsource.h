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

enum e_heatsource_type {
	HS_NONE = 0,	///< No heat source: that's a misconfiguration
	HS_BOILER,		///< boiler type heatsource
};

// XXX cascade
/** Heat source element structure */
struct s_heatsource {
	struct {
		bool configured;		///< true if properly configured
		enum e_runmode runmode;		///< current heatsource set_runmode
		enum e_heatsource_type type;	///< type of heatsource
		unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading -- XXX NOT IMPLEMENTED
		time_t consumer_sdelay;		///< if set, consumers will wait this much time before reducing their consumption (prevents heatsource overheating after e.g. burner run)
	} set;		///< settings (externally set)
	struct {
		bool online;			///< true if source is available for use (under software management)
		bool could_sleep;		///< true if source is could be sleeping (no recent heat request from circuits)
		enum e_runmode runmode;		///< heatsource actual (computed) runmode
		temp_t temp_request;		///< current temperature request for heat source (max of all requests)
		time_t last_run_time;		///< last time heatsource was run
		time_t target_consumer_sdelay;	///< calculated stop delay
		int_fast16_t cshift_crit;	///< critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		int_fast16_t cshift_noncrit;	///< non-critical factor to inhibit (negative) or increase (positive) consummers' heat requests. To be considered a percentage, positive for increased consumption, negative for reduced consumption.
		struct s_temp_intgrl sld_itg;	///< sliding priority integral, used to compute consummer shift when in DHW sliding priority
	} run;		///< private runtime (internally handled)
	char * restrict name;
	void * restrict priv;			///< pointer to source private data structure
	struct {
		int (*online)(struct s_heatsource * const);	///< pointer to source private online() function
		int (*offline)(struct s_heatsource * const);	///< pointer to source private offline() function
		int (*logic)(struct s_heatsource * const);	///< pointer to source private logic() function
		int (*run)(struct s_heatsource * const);	///< pointer to source private run() function
		temp_t (*temp_out)(struct s_heatsource * const);///< pointer to source private temp_out() function (returns current output temperature)
		void (*del_priv)(void * priv);			///< pointer to source private del() function
	} cb;		///< heatsource callbacks
};

int heatsource_online(struct s_heatsource * const heat) __attribute__((warn_unused_result));
int heatsource_offline(struct s_heatsource * const heat);
int heatsource_run(struct s_heatsource * const heat) __attribute__((warn_unused_result));
void heatsource_del(struct s_heatsource * heat);

#endif /* heatsource_h */
