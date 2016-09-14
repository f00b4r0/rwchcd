//
//  rwchcd.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_h
#define rwchcd_h

#include <stddef.h>
#include <stdint.h>	// uint_t
#include <stdbool.h>	// bool
#include <time.h>	// time_t
#include "rwchc_export.h"

#define testbit(var, bit)	((var) & (1 << (bit)))
#define setbit(var, bit)	((var) |= (1 << (bit)))
#define clrbit(var, bit)	((var) &= ~(1 << (bit)))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define dbgmsg(format, ...)	printf("(%s) " format "\n", __func__, ## __VA_ARGS__)
#define dbgerr(format, ...)	printf("(%s) " format "\n", __func__, ## __VA_ARGS__)

enum {
	ALL_OK,
	ENOTCONFIGURED,	///< element is not configured
	ESAFETY,	///< safety error
	EDEADZONE,	///< valve is in deadzone
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
	EGENERIC,
};

#define ON	true
#define OFF	false

#define RWCHCD_TEMPMIN	((-50 + 273) * 100)	///< -50C is the lowest temperature we expect to deal with
#define RWCHCD_TEMPMAX	((200 + 273) * 100)	///< +200C is the highest temperature we expect to deal with

#define RWCHCD_SPI_MAX_TRIES	10	///< how many times SPI ops should be retried

typedef unsigned short	temp_t;		// all temps are internally stored in Kelvin * 100
typedef short		tempid_t;	// temperature index: if negative, is an offset. If > sizeof(Runtime->temps[]), invalid


enum e_runmode {
	RM_OFF = 0,	///< device is fully off, no operation performed (not even frost protection)
	RM_AUTO,	///< device is running based on global plant set_runmode
	RM_COMFORT,	///< device is in comfort mode
	RM_ECO,		///< device is in eco mode
	RM_FROSTFREE,	///< device is in frostfree mode
	RM_MANUAL,	///< device is in manual mode (typically all actuators are on)
	RM_DHWONLY,	///< device is in DHW only mode
};

enum e_systemmode { SYS_OFF = 0, SYS_AUTO, SYS_COMFORT, SYS_ECO, SYS_FROSTFREE, SYS_MANUAL, SYS_DHWONLY };	///< current operation mode

struct s_config {
	bool configured;
	time_t building_tau;		///< building time constant
	short nsensors;			///< number of active sensors (== id of last sensor +1)
	tempid_t id_temp_outdoor;	///< outdoor temp
	temp_t limit_tfrostmin;		///< outdoor temp for frost-protection
	struct rwchc_s_settings rWCHC_settings;
};

struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode runmode;		///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
	bool sleeping;			///< true if no heat request in the past XXX time (plant is asleep)
	float calib_nodac;
	float calib_dac;
	temp_t t_outdoor;
	temp_t t_outdoor_mixed;
	temp_t t_outdoor_attenuated;
	temp_t external_hrequest;	///< external heat request (for cascading) -- XXX NOT IMPLEMENTED
	temp_t temps[RWCHC_NTSENSORS];			///< array of all the system temperatures
	uint16_t rWCHC_sensors[RWCHC_NTSENSORS];	// XXX locks
	union rwchc_u_relays rWCHC_relays;		// XXX locks
	union rwchc_u_outperiphs rWCHC_peripherals;	// XXX locks
	struct s_plant * restrict plant;		///< pointer to scheme structure
	struct s_config * restrict config;	///< running config
	short (*consumer_shift)(void);	///< XXX returns a factor to inhibit (negative) or increase (positive) consummers' heat requests
};

#endif /* rwchcd_h */
