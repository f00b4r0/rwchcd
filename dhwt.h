//
//  dhwt.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT operation API.
 */

#ifndef dhwt_h
#define dhwt_h

#include "rwchcd.h"
#include "pump.h"

/** DHWT element structure */
struct s_dhw_tank {
	struct {
		bool configured;		///< true if properly configured
		unsigned short prio;		///< priority: 0 is highest prio, next positive. For cascading - XXX NOT IMPLEMENTED
		enum e_runmode runmode;		///< dhwt set_runmode
		enum {
			DHWTP_PARALMAX = 0,	///< no priority: parallel run with maximum selection
			DHWTP_PARALDHW,		///< no priority: parallel run with DHW temp request override
			DHWTP_SLIDMAX,		///< sliding priority with maximum selection: a non-critical inhibit signal will be formed if the heatsource cannot sustain the requested temperature
			DHWTP_SLIDDHW,		///< sliding priority with DHW temp request override: a non-critical inhibit signal will be formed if the heatsource cannot sustain the requested temperature
			DHWTP_ABSOLUTE,		///< absolute priority: heating circuits will not receive heat during DHW charge
		} dhwt_cprio;	///< DHW charge priority
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
		bool electric_mode;		///< true if operating on electric heater
		enum e_runmode runmode;		///< dhwt actual (computed) runmode
		temp_t target_temp;		///< current target temp for this tank
		temp_t heat_request;		///< current temp request from heat source for this circuit
		time_t mode_since;		///< starting time of current mode (if charge_on: charge start time, else charge end time)
		int charge_yday;		///< last day forced charge was triggered in DHWTF_FIRST mode
	} run;		///< private runtime (internally handled)
	struct s_pump * restrict feedpump;	///< feed pump for this tank
	struct s_pump * restrict recyclepump;	///< dhw recycle pump for this tank
	char * restrict name;			///< name for this tank
};

int dhwt_online(struct s_dhw_tank * const dhwt) __attribute__((warn_unused_result));
int dhwt_offline(struct s_dhw_tank * const dhwt);
int dhwt_run(struct s_dhw_tank * const dhwt) __attribute__((warn_unused_result));
void dhwt_del(struct s_dhw_tank * restrict dhwt);

/**
 * Test if DHWT is currently in absolute priority charge mode.
 * This function tests if a DHWT in charge is set for absolute priority.
 * @param dhwt target dhwt
 * @return true if DHWT is in active charge and absolute priority is set.
 */
static inline bool dhwt_in_absolute_charge(const struct s_dhw_tank * const dhwt)
{
	if (dhwt->run.charge_on && (DHWTP_ABSOLUTE == dhwt->set.dhwt_cprio))
		return (true);
	else
		return (false);
}

#endif /* dhwt_h */
