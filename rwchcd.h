//
//  rwchcd.h
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_h
#define rwchcd_h

/**
 * @file
 * Main API.
 */

#include <stddef.h>
#include <stdint.h>	// uint_t
#include <stdbool.h>	// bool
#include <time.h>	// time_t
#include <stdio.h>	// (f)printf
#include <pthread.h>	// rwlocks

#define testbit(var, bit)	((var) & (1 << (bit)))
#define setbit(var, bit)	((var) |= (1 << (bit)))
#define clrbit(var, bit)	((var) &= ~(1 << (bit)))
#define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))

#define SETorDEF(set, def)	(set ? set : def)

/* i18n stuff */
#ifdef HAVE_GETTEXT
 #include <libintl.h>
 #define _(String)      gettext(String)
#else
 #define _(String)      String
#endif  /* HAVE_GETTEXT */

#ifdef DEBUG
 #define dbgmsg(format, ...)	printf("[%s:%d] (%s()) " format "\n", __FILE__, __LINE__, __func__, ## __VA_ARGS__)
 #define pr_log(format, ...)	fprintf(stderr, format "\n", ## __VA_ARGS__)
#else
 #define dbgmsg(format, ...)	/* nothing */
 #define pr_log(format, ...)	printf(format "\n", ## __VA_ARGS__)
#endif

#define dbgerr(format, ...)	fprintf(stderr, "(%ld) ERROR! [%s:%d] (%s()) " format "\n", time(NULL), __FILE__, __LINE__, __func__, ## __VA_ARGS__)

/** Valid execution status (used as negative return values) */
enum e_execs {
	ALL_OK = 0,	///< no error (must be 0)
	ENOTCONFIGURED,	///< element is not configured
	EMISCONFIGURED,	///< invalid configuration settings
	ESAFETY,	///< safety error
	EDEADZONE,	///< target is in deadzone
	EDEADBAND,	///< valve is in deadband
	ENOTIMPLEMENTED,///< argument/request/whatever is not implemented
	EOFFLINE,	///< device is offline
	EINVALID,	///< invalid argument
	EINVALIDMODE,	///< invalid set_runmode
	ESENSORINVAL,	///< invalid sensor id
	ESENSORSHORT,	///< sensor is shorted
	ESENSORDISCON,	///< sensor is disconnected
	ESPI,		///< SPI problem
	EINIT,		///< initialization problem
	EOOM,		///< Out of memory
	EEXISTS,	///< Object already exists (id conflict)
	ETRUNC,		///< Truncation occured (LCD output)
	ESTORE,		///< Storage errors
	EMISMATCH,	///< version mismatch
	EGENERIC,
};

#define ON	true
#define OFF	false

// fixed-point precision: we use a 1/1000th of a degree to reduce rounding imprecision in calculations
#define KPRECISIONI	1000
#define KPRECISIONF	1000.0F

#define RWCHCD_TEMPMIN	((-50 + 273) * KPRECISIONI)	///< -50C is the lowest temperature we expect to deal with
#define RWCHCD_TEMPMAX	((150 + 273) * KPRECISIONI)	///< +150C is the highest temperature we expect to deal with

typedef int_fast32_t	temp_t;		///< all temps are internally stored in Kelvin * KPRECISION (32bit avoids overflow with disconnected sensors). Must be signed for maths
typedef int_fast16_t	tempid_t;	///< temperature index: if negative, is an offset in Kelvin. If > sizeof(Runtime->temps[]), invalid
typedef uint_fast8_t	relid_t;	///< relay id matching hardware: 1 to 14, with 13==RL1 and 14==RL2

/** Valid run modes */
enum e_runmode {
	RM_OFF = 0,	///< device is fully off, no operation performed (not even frost protection)
	RM_AUTO,	///< device is running based on global plant set_runmode
	RM_COMFORT,	///< device is in comfort mode
	RM_ECO,		///< device is in eco mode
	RM_FROSTFREE,	///< device is in frostfree mode
	RM_DHWONLY,	///< device is in DHW only mode
	RM_MANUAL,	///< device is in manual mode (typically all actuators are on)
	RM_UNKNOWN,	///< invalid past this value
};

/** Valid system modes */
enum e_systemmode {
	SYS_OFF = 0,	///< system is fully off
	SYS_AUTO,	///< system is running in automatic mode
	SYS_COMFORT,	///< system is running in comfort mode
	SYS_ECO,	///< system is running in eco mode
	SYS_FROSTFREE,	///< system is running in frostfree mode
	SYS_DHWONLY,	///< system is running in DHW only mode
	SYS_MANUAL,	///< system is running in manual mode
	SYS_UNKNOWN,	///< invalid past this value
};

/** Circuit common parameters */
struct s_circuit_params {
	temp_t t_comfort;		///< target ambient temp in comfort mode
	temp_t t_eco;			///< target ambient temp in eco mode
	temp_t t_frostfree;		///< target ambient temp in frost-free mode
	temp_t t_offset;		///< global offset adjustment for ambient targets
	temp_t outhoff_comfort;		///< outdoor temp for no heating in comfort mode
	temp_t outhoff_eco;		///< outdoor temp for no heating in eco mode
	temp_t outhoff_frostfree;	///< outdoor temp for no heating in frostfree mode
	temp_t outhoff_histeresis;	///< histeresis for no heating condition
	temp_t limit_wtmin;		///< minimum water pipe temp when this circuit is active (e.g. for frost protection)
	temp_t limit_wtmax;		///< maximum allowed water pipe temp when this circuit is active. @warning MUST be set either globally or locally otherwise circuit won't heat
	temp_t temp_inoffset;		///< offset temp for heat source request. @note beware of interaction with e.g. boiler histeresis
};

/** DHWT common parameters */
struct s_dhwt_params {
	time_t limit_chargetime;	///< maximum duration of charge time
	temp_t limit_wintmax;		///< maximum allowed water intake temp when active
	temp_t limit_tmin;		///< minimum dhwt temp when active (e.g. for frost protection). @warning MUST be set either globally or locally otherwise dhwt won't heat
	temp_t limit_tmax;		///< maximum allowed dhwt temp when active. @warning MUST be set either globally or locally otherwise dhwt won't heat
	temp_t t_legionella;		///< target temp for legionella prevention. Will override limit_tmin and limit_tmax @todo XXX NOT IMPLEMENTED
	temp_t t_comfort;		///< target temp in comfort mode. - XXX setup ensure > tfrostfree
	temp_t t_eco;			///< target temp in eco mode. - XXX setup ensure > tfrostfree
	temp_t t_frostfree;		///< target temp in frost-free mode. - XXX setup ensure > 0C
	temp_t histeresis;		///< histeresis for target temp. - XXX setup ensure > 0C
	temp_t temp_inoffset;		///< offset temp for heat source request. - XXX setup ensure > 0C
};

/** Config structure */
struct s_config {
	bool restored;			///< true if config has been restored from storage
	bool configured;		///< true if properly configured
	bool summer_pump_maintenance;	///< true if pumps should be run periodically in summer. @todo XXX NOT IMPLEMENTED
	time_t building_tau;		///< building time constant
	int_fast16_t nsensors;		///< number of active sensors (== id of last sensor)
	tempid_t id_temp_outdoor;	///< outdoor temp
	temp_t set_temp_outdoor_offset;	///< offset for outdoor temp sensor
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	temp_t limit_tfrost;		///< outdoor temp for plant frost protection
	struct s_circuit_params def_circuit;	///< circuit defaults: if individual circuits don't set these values, these defaults will be used
	struct s_dhwt_params def_dhwt;		///< DHWT defaults: if individual dhwts don't set these values, these defaults will be used
};

#define	RWCHCD_NTEMPS	15	///< number of available sensors

/** Runtime environment structure */
struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode runmode;		///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
	bool plant_could_sleep;		///< true if all heat sources could sleep (plant could sleep)
	bool summer;			///< outdoor temperature is compatible with summer mode
	bool frost;			///< outdoor temperature requires frost protection
	temp_t t_outdoor;		///< instantaneous outdoor temperature
	temp_t t_outdoor_60;		///< t_outdoor filtered over a 60s window
	time_t t_outdoor_ltime;		///< time at which t_outdoor_filtered and t_outdoor_attenuated were last updated
	temp_t t_outdoor_filtered;	///< t_outdoor filtered by building time constant
	temp_t t_outdoor_mixed;		///< mixed outdoor temperature (average of t_outdoor and t_filtered: the moving average of t_outdoor with building_tau)
	temp_t t_outdoor_attenuated;	///< attenuated outdoor temperature (moving average of t_filtered with building_tau: double filter on t_outdoor)
	temp_t external_hrequest;	///< external heat request (for cascading). @todo XXX NOT IMPLEMENTED
	time_t start_time;		///< system start time
	time_t consumer_stop_delay;	///< minimum time consumers should keep their current consumption before turning off
	int_fast16_t consumer_shift;	///< a factor to inhibit (negative) or increase (positive) consummers' heat requests. @todo XXX REVIEW
	struct s_plant * restrict plant;	///< running plant
	struct s_config * restrict config;	///< running config
	temp_t temps[RWCHCD_NTEMPS];		///< array of all the system temperatures
	pthread_rwlock_t runtime_rwlock;///< @note having this here prevents using "const" in instances where it would otherwise be possible
};

#endif /* rwchcd_h */
