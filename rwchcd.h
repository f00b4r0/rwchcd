//
//  rwchcd.h
//  
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

#ifndef rwchcd_h
#define rwchcd_h

#include <stddef.h>
#include <stdint.h>	// uint_t
#include <stdbool.h>	// bool
#include <time.h>	// time_t
#include <stdio.h>	// printf
#include "rwchc_export.h"

#define testbit(var, bit)	((var) & (1 << (bit)))
#define setbit(var, bit)	((var) |= (1 << (bit)))
#define clrbit(var, bit)	((var) &= ~(1 << (bit)))
#define ARRAY_SIZE(x)		(sizeof(x) / sizeof(x[0]))

/* i18n stuff */
#ifdef HAVE_GETTEXT
 #include <libintl.h>
 #define _(String)      gettext(String)
#else
 #define _(String)      String
#endif  /* HAVE_GETTEXT */

#define dbgmsg(format, ...)	printf("[%s:%d] (%s()) " format "\n", __FILE__, __LINE__, __func__, ## __VA_ARGS__)
#define dbgerr(format, ...)	printf("ERROR! (%s()) " format "\n", __func__, ## __VA_ARGS__)

enum {
	ALL_OK,
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
	EGENERIC,
};

#define ON	true
#define OFF	false

#define RWCHCD_TEMPMIN	((-50 + 273) * 100)	///< -50C is the lowest temperature we expect to deal with
#define RWCHCD_TEMPMAX	((150 + 273) * 100)	///< +150C is the highest temperature we expect to deal with

#define RWCHCD_SPI_MAX_TRIES	5	///< how many times SPI ops should be retried

typedef int_fast32_t	temp_t;		// all temps are internally stored in Kelvin * 100 (32bit avoids overflow with disconnected sensors). Must be signed for maths
typedef int_fast16_t	tempid_t;	// temperature index: if negative, is an offset in Kelvin. If > sizeof(Runtime->temps[]), invalid


enum e_runmode {
	RM_OFF = 0,	///< device is fully off, no operation performed (not even frost protection)
	RM_AUTO,	///< device is running based on global plant set_runmode
	RM_COMFORT,	///< device is in comfort mode
	RM_ECO,		///< device is in eco mode
	RM_FROSTFREE,	///< device is in frostfree mode
	RM_DHWONLY,	///< device is in DHW only mode
	RM_MANUAL,	///< device is in manual mode (typically all actuators are on)
};

enum e_systemmode {
	SYS_OFF = 0,	///< system is fully off
	SYS_AUTO,	///< system is running in automatic mode
	SYS_COMFORT,	///< system is running in comfort mode
	SYS_ECO,	///< system is running in eco mode
	SYS_FROSTFREE,	///< system is running in frostfree mode
	SYS_DHWONLY,	///< system is running in DHW only mode
	SYS_MANUAL,	///< system is running in manual mode
};

struct s_config {
	bool configured;
	time_t building_tau;		///< building time constant
	int_fast16_t nsensors;			///< number of active sensors (== id of last sensor +1)
	tempid_t id_temp_outdoor;	///< outdoor temp
	temp_t set_temp_outdoor_offset;	///< offset for outdoor temp sensor
	temp_t limit_tfrostmin;		///< outdoor temp for frost-protection
	temp_t limit_tsummer;		///< outdoor temp for summer switch over
	bool summer_pump_maintenance;	///< true if pumps should be run periodically in summer - XXX NOT IMPLEMENTED
	struct rwchc_s_settings rWCHC_settings;
};

struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode runmode;		///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
	bool sleeping;			///< true if all heat sources are sleeping (plant is asleep)
	bool summer;			///< outdoor temperature is compatible with summer mode - XXX NOT IMPLEMENTED
	float calib_nodac;		///< sensor calibration value without dac offset
	float calib_dac;		///< sensor calibration value with dac offset
	temp_t t_outdoor;		///< instantaneous outdoor temperature
	temp_t t_outdoor_mixed;		///< mixed outdoor temperature (average of t_outdoor and t_filtered: the moving average of t_outdoor with building_tau)
	temp_t t_outdoor_attenuated;	///< attenuated outdoor temperature (moving average of t_filtered with building_tau: double filter on t_outdoor)
	temp_t external_hrequest;	///< external heat request (for cascading) -- XXX NOT IMPLEMENTED
	struct s_plant * restrict plant;	///< running plant
	struct s_config * restrict config;	///< running config
	short (*consumer_shift)(void);	///< XXX returns a factor to inhibit (negative) or increase (positive) consummers' heat requests
	temp_t temps[RWCHC_NTSENSORS];			///< array of all the system temperatures
	rwchc_sensor_t rWCHC_sensors[RWCHC_NTSENSORS];	// XXX locks
	union rwchc_u_relays rWCHC_relays;		// XXX locks
	union rwchc_u_outperiphs rWCHC_peripherals;	// XXX locks
};

#endif /* rwchcd_h */
