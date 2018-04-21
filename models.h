//
//  models.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models implementation API.
 */

#ifndef rwchcd_models_h
#define rwchcd_models_h

#include "rwchcd.h"

/** Models */
struct s_models {
	bool online;			///< true if the models can be run
	uint_fast8_t bmodels_n;		///< number of building models
	struct s_bmodel_l * restrict bmodels;	///< building models
};

/** List of building models */
struct s_bmodel_l {
	uint_fast8_t id;
	struct s_bmodel * restrict bmodel;
	struct s_bmodel_l * next;
};

/** building model */
struct s_bmodel {
	struct {
		bool configured;	///< true if configured
		time_t tau;		///< bmodel time constant
		tempid_t tid_outdoor;	///< outdoor sensor id for this bmodel. @note value will be smoothed over 60s
	} set;
	struct {
		bool online;		///< true if bmodel is online
		bool summer;		///< true if summer mode conditions are met
		bool frost;		///< true if frost conditions are met
		time_t t_out_ltime;	///< last update time for t_out
		time_t t_out_faltime;	///< time at which t_outdoor_filtered and t_outdoor_attenuated were last updated
		temp_t t_out;		///< current outdoor temperature
		temp_t t_out_filt;	///< t_outdoor filtered by bmodel time constant
		temp_t t_out_mix;	///< mixed outdoor temperature (average of t_outdoor and t_filtered: the moving average of t_outdoor with tau)
		temp_t t_out_att;	///< attenuated outdoor temperature (moving average of t_filtered with tau: double filter on t_outdoor)
	} run;
	char * restrict name;		///< name for this bmodel
};

struct s_bmodel * models_new_bmodel(const char * restrict const name);
const struct s_bmodel * models_fbn_bmodel(const char * restrict const name);
int models_init(void);
void models_exit(void);
int models_online(void);
int models_offline(void);
int models_run(void);

temp_t models_outtemp(void) __attribute__ ((deprecated));

#endif /* rwchcd_models_h */
