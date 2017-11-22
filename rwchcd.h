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

#define snprintf_needed(format, ...)	(1+snprintf(NULL, 0, format, __VA_ARGS__))
#define snprintf_automalloc(target, size, format, ...)			({\
		size = snprintf_needed(format, __VA_ARGS__);		\
		target = malloc(size);					\
		if (target)						\
			snprintf(target, size, format, __VA_ARGS__);	})

/** Valid execution status (used as negative return values) */
enum e_execs {
	ALL_OK = 0,	///< no error (must be 0)
	ENOTCONFIGURED,	///< element is not configured
	EMISCONFIGURED,	///< invalid configuration settings
	ESAFETY,	///< safety error
	EDEADZONE,	///< target is in deadzone
	EDEADBAND,	///< valve is in deadband
	ENOTIMPLEMENTED,///< argument/request/whatever is not implemented
	EOFFLINE,	///< element is offline: a critical operational prerequisite failed
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

// fixed-point precision: we use a 1/1000th of a degree (millikelvin) to reduce rounding imprecision in calculations
#define KPRECISIONI	1000
#define KPRECISIONF	1000.0F

#define RWCHCD_TEMPMIN	((-50 + 273) * KPRECISIONI)	///< -50C is the lowest temperature we expect to deal with
#define RWCHCD_TEMPMAX	((150 + 273) * KPRECISIONI)	///< +150C is the highest temperature we expect to deal with

#define	RWCHCD_NTEMPS	15	///< number of available sensors

#define RWCHCD_TEMP_NOREQUEST	0		///< value for no heat request
#define RWCHCD_CSHIFT_MAX	200		///< Maximum value for consumer shift

/** Specific error values for temps */
enum {
	TEMPUNSET = 0,	///< temp hasn't been fetched
	TEMPSHORT,	///< sensor is shorted
	TEMPDISCON,	///< sensor is disconnected
	TEMPINVALID,	///< values below this are all invalid
};

typedef int_fast32_t	temp_t;		///< all temps are internally stored in Kelvin * KPRECISION (32bit avoids overflow with disconnected sensors). Must be signed for maths
typedef uint_fast8_t	tempid_t;	///< temperature index: if > sizeof(Runtime->temps[]), invalid
typedef uint_fast8_t	relid_t;	///< relay id matching hardware: 1 to 14, with 13==RL1 and 14==RL2

/** Valid run modes */
enum e_runmode {
	RM_OFF = 0,	///< device is fully off, no operation performed (not even frost protection)
	RM_AUTO,	///< device is running based on global plant set_runmode
	RM_COMFORT,	///< device is in comfort mode
	RM_ECO,		///< device is in eco mode
	RM_FROSTFREE,	///< device is in frostfree mode
	RM_DHWONLY,	///< device is in DHW only mode
	RM_TEST,	///< device is in test mode (typically all actuators are on)
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
	SYS_TEST,	///< system is running in test mode
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

#endif /* rwchcd_h */
