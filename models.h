//
//  models.h
//  rwchcd
//
//  (C) 2017,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models implementation API.
 */

#ifndef rwchcd_models_h
#define rwchcd_models_h

#include <stdatomic.h>

#include "rwchcd.h"
#include "timekeep.h"
#include "io/inputs.h"

typedef uint_fast8_t 	modid_t;
#define MODID_MAX	UINT_FAST8_MAX

/** building model */
struct s_bmodel {
	struct {
		bool configured;	///< true if configured
		bool log;		///< true if logging must be enabled for this bmodel
		temp_t limit_tsummer;	///< outdoor temp for summer switch over
		temp_t limit_tfrost;	///< outdoor temp for frost protection
		timekeep_t tau;		///< bmodel time constant
		itid_t tid_outdoor;	///< outdoor sensor id for this bmodel. @note value will be smoothed over 60s
	} set;
	struct {
		atomic_bool online;	///< true if bmodel is online
		atomic_bool summer;	///< true if summer mode conditions are met
		atomic_bool frost;	///< true if frost conditions are met
		timekeep_t t_out_ltime;	///< last update time for t_out
		timekeep_t t_out_faltime;///< time at which t_outdoor_filtered and t_outdoor_attenuated were last updated
		_Atomic temp_t t_out;	///< current outdoor temperature (smoothed over 60s)
		_Atomic temp_t t_out_filt;///< t_outdoor filtered by bmodel time constant (moving average of t_outdoor with tau)
		_Atomic temp_t t_out_mix;///< mixed outdoor temperature (average of t_outdoor and t_filtered)
		_Atomic temp_t t_out_att;///< attenuated outdoor temperature (moving average of t_filtered with tau: double filter on t_outdoor)
	} run;
	const char * restrict name;	///< unique name for this bmodel
};

/** Models */
struct s_models {
	struct {
		struct s_bmodel * all;
		modid_t last;
		modid_t n;
	} bmodels;
	bool online;			///< true if the models can be run
};

const struct s_bmodel * models_fbn_bmodel(const char * restrict const name);
int models_init(void);
void models_exit(void);
int models_online(void);
int models_offline(void);
int models_run(void);

temp_t models_outtemp(void) __attribute__ ((deprecated));

#endif /* rwchcd_models_h */
