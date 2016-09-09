//
//  rwchcd.h
//  
//
//  Created by Thibaut VARENE on 06/09/16.
//
//

#ifndef rwchcd_h
#define rwchcd_h

#include <stdint.h>
#include <stdbool.h>
#include "rwchc_export.h"

#define testbit(var, bit)	((var) & (1 << (bit)))
#define setbit(var, bit)	((var) |= (1 << (bit)))
#define clrbit(var, bit)	((var) &= ~(1 << (bit)))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

enum {
	ALL_OK,
	ENOTCONFIGURED,
	ESAFETY,
	EDEADZONE,
	ENOTIMPLEMENTED,
	EOFFLINE,	///< device is offline
	EINVALID,	///< invalid argument
	EINVALIDMODE,	///< invalid set_runmode
	ESENSORINVAL,	///< invalid sensor id
	ESENSORSHORT,	///< sensor is shorted
	ESENSORDISCON,	///< sensor is disconnected
	EGENERIC,
};

#define ON	true
#define OFF	false

#define RWCHC_TEMPMIN	XXX
#define RWCHC_TEMPMAX	XXX

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

enum e_systemmode { OFF = 0, AUTO, COMFORT, ECO, FROSTFREE, MANUAL, DHWONLY };	///< current operation mode

struct s_config {
	bool configured;
	short nsensors;		///< number of active sensors (== id of last sensor +1)
	short scheme_type;		///< hydraulic scheme type - UNUSED
	enum e_runmode set_runmode;	///< desired operation mode
	temp_t limit_tfrostmin;	///< outdoor temp for frost-protection
	time_t building_tau;	///< building time constant
	tempid_t id_temp_outdoor;	///< outdoor temp
	struct rwchc_s_settings rWCHC_settings;
};

struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode set_runmode;	///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
	bool sleeping;			///< true if no heat request in the past XXX time (plant is asleep)
	float calib_nodac;
	float calib_dac;
	temp_t t_outdoor;
	temp_t t_outdoor_mixed;
	temp_t t_outdoor_attenuated;
	temp_t external_hrequest;	///< external heat request (for cascading) -- XXX NOT IMPLEMENTED
	temp_t temps[];				///< array of all the system temperatures
	uint16_t rWCHC_sensors[RWCHC_NTSENSORS];	// XXX locks
	union rwchc_u_relays rWCHC_relays;		// XXX locks
	union rwchc_u_outperiphs rWCHC_peripherals;	// XXX locks
	struct s_plant * restrict plant;		///< pointer to scheme structure
	struct s_config * restrict config;	///< running config
	short (*consumer_shift)(void);	///< XXX returns a factor to inhibit (negative) or increase (positive) consummers' heat requests
};

inline struct s_runtime * get_runtime(void);

#endif /* rwchcd_h */
