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

/** building model */
struct s_bmodel {
	struct {
		bool configured;	///< true if configured
		time_t tau;		///< bmodel time constant
	} set;
	struct {
		bool summer;
		time_t t_out_ltime;	///< time at which t_outdoor_filtered and t_outdoor_attenuated were last updated
		temp_t t_out_filt;	///< t_outdoor filtered by bmodel time constant
		temp_t t_out_mix;	///< mixed outdoor temperature (average of t_outdoor and t_filtered: the moving average of t_outdoor with tau)
		temp_t t_out_att;	///< attenuated outdoor temperature (moving average of t_filtered with tau: double filter on t_outdoor)
	} run;
	char * restrict name;		///< name for this bmodel
};

/** List of building models */
struct s_bmodel_l {
	uint_fast8_t id;
	struct s_bmodel * restrict bmodel;
	struct s_bmodel_l * next;
};

/** List of models */
struct s_models {
	bool configured;		///< true if the models are configured
	bool online;			///< true if the models can be run
	uint_fast8_t bmodels_n;		///< number of building models
	struct s_bmodel_l * restrict bmodels;	///< building models
};

struct s_bmodel * models_new_bmodel(struct s_models * restrict const models, const char * restrict const name);
struct s_models * models_new(void);
void models_del(struct s_models * models);
int models_online(struct s_models * restrict const models);
int models_offline(struct s_models * restrict const models);
int models_run(struct s_models * restrict const models);

#endif /* rwchcd_models_h */
