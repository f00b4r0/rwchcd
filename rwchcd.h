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
	EINVALIDMODE,	///< invalid runmode
	ESENSORINVAL,	///< invalid sensor id
	ESENSORSHORT,	///< sensor is shorted
	ESENSORDISCON,	///< sensor is disconnected
	EGENERIC,
};

#define ON	true
#define OFF	false

#define RWCHC_TEMPMIN	XXX
#define RWCHC_TEMPMAX	XXX

typedef temp_t	unsigned short;		// all temps are internally stored in Kelvin * 100
typedef tempid_t	short;		// temperature index: if negative, is an offset. If > sizeof(Runtime->temps[]), invalid


enum e_runmode { RM_OFF = 0, RM_AUTO, RM_COMFORT, RM_ECO, RM_FROSTFREE, RM_MANUAL, RM_DHWONLY };
enum e_systemmode { OFF = 0, AUTO, COMFORT, ECO, FROSTFREE, MANUAL, DHWONLY };	///< current operation mode

struct s_config {
	bool configured;
	short nsensors;		///< number of active sensors (== id of last sensor +1)
	short scheme_type;		///< hydraulic scheme type - UNUSED
	enum e_runmode runmode;	///< desired operation mode
	temp_t limit_tfrostmin;	///< outdoor temp for frost-protection
	time_t building_tau;	///< building time constant
	temp_t histeresis;
	tempid_t id_temp_outdoor;	///< outdoor temp
	struct rwchc_s_settings rWCHC_settings;
};

struct s_runtime {
	enum e_systemmode systemmode;	///< current operation mode
	enum e_runmode runmode;		///< CANNOT BE RM_AUTO
	enum e_runmode dhwmode;		///< CANNOT BE RM_AUTO or RM_DHWONLY
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
	struct s_plant * plant;		///< pointer to scheme structure
	struct s_config * config;	///< running config
};

inline struct s_runtime * get_runtime(void);

#endif /* rwchcd_h */
