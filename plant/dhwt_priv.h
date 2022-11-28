//
//  plant/dhwt_priv.h
//  rwchcd
//
//  (C) 2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT internal API.
 */

#ifndef dhwt_priv_h
#define dhwt_priv_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "timekeep.h"
#include "scheduler.h"
#include "io/inputs.h"
#include "io/outputs.h"

/** DHWT element structure */
struct s_dhwt {
	struct {
		bool configured;		///< if true, DHWT is properly configured
		bool log;			///< true if data logging should be enabled for this dhwt. *Defaults to false*
		bool electric_hasthermostat;	///< true if electric self-heating has its own thermostat. When switching to electric, self-heating relay will be permanently on; and if none of the DHWT sensors are working the electric self-heating relay will be turned on in all active modes. *Defaults to false*
		bool anti_legionella;		///< if true, anti-legionella heat charge is allowed for this DHWT. *Defaults to false*
		bool legionella_recycle;	///< if true, recycle pump will be turned on during anti-legionella heat charge. *Defaults to false*
		bool electric_recycle;		///< if true, recycle pump will be turned on while the DHWT operates in electric self-heating. *Defaults to false*
		uint_fast8_t prio;		///< priority: 0 (*default*) is highest prio, next positive. For cascading: DHWT with lower prio (higher value) will only be charged if @b none of the DHWTs with higer prio (lower value) are charging
		schedid_t schedid;		///< schedule id for this DHWT. *Optional*
		enum e_runmode runmode;		///< dhwt set_runmode. *REQUIRED*
		enum {
			DHWTP_PARALMAX = 0,	///< no priority: parallel run with maximum selection. Config `paralmax`. *DEFAULT*.
			DHWTP_PARALDHW,		///< no priority: parallel run with DHW temp request override. Config `paraldhw`
			DHWTP_SLIDMAX,		///< sliding priority with maximum selection: a non-critical inhibit signal will be formed if the heatsource cannot sustain the requested temperature. Config `slidmax`
			DHWTP_SLIDDHW,		///< sliding priority with DHW temp request override: a non-critical inhibit signal will be formed if the heatsource cannot sustain the requested temperature. Config `sliddhw`
			DHWTP_ABSOLUTE,		///< absolute priority: heating circuits will not receive heat during DHW charge. Config `absolute`
		} dhwt_cprio;	///< DHW charge priority. *Optional*
		enum {
			DHWTF_NEVER = 0,	///< never force. Config `never`. *DEFAULT*
			DHWTF_FIRST,		///< force first comfort charge of the day. Config `first`
			DHWTF_ALWAYS		///< force all comfort charges. Config `always`
		} force_mode;	///< Programmed force charge when switching to #RM_COMFORT. *Optional*
		itid_t tid_bottom;		///< temp sensor id at bottom of dhw tank. *Optional* if #tid_top is set, *Required* otherwise
		itid_t tid_top;			///< temp sensor id at top of dhw tank. *Optional* if #tid_bottom is set, *Required* otherwise
		itid_t tid_win;			///< temp sensor id heatwater inlet. *Required* if #p.pump_feed is set. @note must @b NOT rely on pump_feed operation for accurate temp read.
		orid_t rid_selfheater;		///< relay for internal electric heater (if available). *Optional*
		temp_t tthresh_dhwisol;		///< threshold temperature below wich the DHW isol valve will be closed and/or the recycling pump stopped. Hysteresis +1K for reverse operation. *Optional*, only makes sense if valve and/or pump are available.
		struct s_dhwt_params params;	///< local parameter overrides. @note if a default is set in config, it will prevail over any unset (0) value here: to locally set 0 value as "unlimited", set it to max. Some settings must be set either globally or locally.
		struct {
			struct s_pump * restrict pump_feed;	///< feed pump for this tank. *Optional*
			struct s_pump * restrict pump_dhwrecycle;	///< dhw recycle pump for this tank. *Optional*
			struct s_valve * restrict valve_feedisol;	///< Isolation valve used to disconnect the DHWT from the heatwater flow. *Optional*.
			struct s_valve * restrict valve_dhwisol;	///< Isolation valve used to disconnect the DHWT from the DHW circuit. *Optional*.
		} p;		///< pointer-based settings. For configuration details see specific types instructions
	} set;		///< settings (externally set)
	struct {
		atomic_bool online;		///< true if tank is available for use (under software management)
		bool active;			///< true if tank is active
		atomic_bool charge_on;		///< true if charge ongoing
		atomic_bool recycle_on;		///< true if recycle pump should be running.
		atomic_bool force_on;		///< true if charge should be forced even if current temp is above the charge threshold (but below the target)
		atomic_bool legionella_on;	///< true if anti-legionella charge is required
		bool charge_overtime;		///< true if charge went overtime
		atomic_bool electric_mode;	///< true if operating on electric heater
		_Atomic enum e_runmode runmode;	///< dhwt actual (computed) runmode
		_Atomic temp_t target_temp;	///< current target temp for this tank
		_Atomic temp_t actual_temp;	///< current temperature as retained by the software (could be top or bottom)
		temp_t heat_request;		///< current temp request from heat source for this circuit
		timekeep_t mode_since;		///< starting time of current mode (if #charge_on: charge start time, else charge end time)
		timekeep_t floor_until_time;	///< non-zero if the current intake should not be reduced until this future timestamp
		int charge_yday;		///< last day forced charge was triggered in #DHWTF_FIRST mode
	} run;		///< private runtime (internally handled)
	struct {
		atomic_bool o_runmode;		///< true if set.runmode should be overriden by overrides.runmode
		_Atomic enum e_runmode runmode;	///< runmode override (applied if o_runmode is set)
	} overrides;	///< overrides (used for temporary settings override via e.g. dbus calls)
	const struct s_pdata * pdata;		///< read-only plant data for this tank
	const char * restrict name;		///< unique name for this tank
	enum e_execs status;			///< last known status
};


#endif /* dhwt_priv_h */
