//
//  models.c
//  rwchcd
//
//  (C) 2017-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models implementation.
 * This file implements basic building models based on time constant.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "storage.h"
#include "hardware.h"
#include "alarms.h"
#include "models.h"

#define OUTDOOR_SMOOTH_TIME		60	///< time in seconds over which outdoor temp is smoothed
#define OUTDOOR_AVG_UPDATE_DT		600	///< prevents running averages at less than 10mn interval. Should be good up to 100h tau.
#define MODELS_STORAGE_NAME_LEN		64
#define MODELS_STORAGE_BMODEL_PREFIX	"models_bmodel_"

/** List of building models */
struct s_bmodel_l {
	uint_fast8_t id;
	struct s_bmodel * restrict bmodel;
	struct s_bmodel_l * next;
};

static const storage_version_t Models_sversion = 3;

/**
 * Save building model state to permanent storage.
 * @bug name length hack
 * @param bmodel the building model to save, @b MUST be named
 * @return exec status
 */
static int bmodel_save(const struct s_bmodel * restrict const bmodel)
{
	char buf[MODELS_STORAGE_NAME_LEN] = MODELS_STORAGE_BMODEL_PREFIX;

	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	// can't store if no name
	if (!bmodel->name)
		return (-EINVALID);

	strncat(buf, bmodel->name, MODELS_STORAGE_NAME_LEN-strlen(buf)-1);

	return (storage_dump(buf, &Models_sversion, &bmodel->run, sizeof(bmodel->run)));
}

/**
 * Restore building model state from permanent storage.
 * @bug name length hack
 * @param bmodel the building model to restore, @b MUST be named
 * @return exec status
 */
static int bmodel_restore(struct s_bmodel * restrict const bmodel)
{
	char buf[MODELS_STORAGE_NAME_LEN] = MODELS_STORAGE_BMODEL_PREFIX;
	struct s_bmodel temp_bmodel;
	storage_version_t sversion;
	int ret;

	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	// can't restore if no name
	if (!bmodel->name)
		return (-EINVALID);

	strncat(buf, bmodel->name, MODELS_STORAGE_NAME_LEN-strlen(buf)-1);

	// try to restore key elements
	ret = storage_fetch(buf, &sversion, &temp_bmodel.run, sizeof(temp_bmodel.run));
	if (ALL_OK == ret) {
		if (Models_sversion != sversion)
			return (-EMISMATCH);

		bmodel->run.summer = temp_bmodel.run.summer;
		bmodel->run.frost = temp_bmodel.run.frost;
		bmodel->run.t_out_faltime = temp_bmodel.run.t_out_faltime;
		bmodel->run.t_out_filt = temp_bmodel.run.t_out_filt;
		bmodel->run.t_out_mix = temp_bmodel.run.t_out_mix;
		bmodel->run.t_out_att = temp_bmodel.run.t_out_att;
	}

	return (ret);
}

/**
 * Find a building model by name.
 * @param bmodels list of bmodels to search
 * @param name target name to find
 * @return bmodel if found, NULL otherwise
 */
static const struct s_bmodel * bmodels_fbn(const struct s_bmodel_l * const bmodels, const char * const name)
{
	const struct s_bmodel_l * bml;
	struct s_bmodel * bmodel = NULL;

	for (bml = bmodels; bml; bml = bml->next) {
		if (!strcmp(bml->bmodel->name, name)) {
			bmodel = bml->bmodel;
			break;
		}
	}

	return (bmodel);
}

/**
 * Delete a building model.
 * @param bmodel target model
 */
static void bmodel_del(struct s_bmodel * restrict bmodel)
{
	if (!bmodel)
		return;

	free(bmodel->name);
	bmodel->name = NULL;
	free(bmodel);
}

/**
 * Process outdoor temperature.
 * Computes "smoothed" outdoor temperature, with a safety fallback in case of sensor failure.
 * @note must run at (ideally fixed) intervals >= 1s
 * @param bmodel target building model
 */
static void bmodel_outdoor_temp(struct s_bmodel * restrict const bmodel)
{
	const time_t last = bmodel->run.t_out_ltime;	// previous sensor time. At first run: 0 which makes mavg return new sample
	time_t dt;
	temp_t toutdoor;
	int ret;

	ret = hardware_sensor_clone_temp(bmodel->set.id_t_out, &toutdoor);
	if (ALL_OK == ret) {
		hardware_sensor_clone_time(bmodel->set.id_t_out, &bmodel->run.t_out_ltime);
		dt = bmodel->run.t_out_ltime - last;
		bmodel->run.t_out = temp_expw_mavg(bmodel->run.t_out, toutdoor, OUTDOOR_SMOOTH_TIME, dt);
	}
	else {
		// in case of outdoor sensor failure, assume outdoor temp is tfrost-1: ensures frost protection
		bmodel->run.t_out = get_runtime()->config->limit_tfrost-1;
		alarms_raise(ret, _("Outdoor sensor failure"), _("Outdr sens fail"));
	}
}

/**
 * Process the building model outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * This function is designed so that at init time, when the variables are all 0,
 * the averages will take the value of the current outdoor temperature.
 * http://liu.diva-portal.org/smash/get/diva2:893577/FULLTEXT01.pdf
 * http://www.ibpsa.org/proceedings/BS2013/p_2030.pdf
 * http://www.wseas.us/e-library/conferences/2013/Brasov/ACMOS/ACMOS-32.pdf
 * http://www.emu.systems/en/blog/2015/10/19/whats-the-time-constant-of-a-building
 * https://books.google.fr/books?id=dIYxQkS_SWMC&pg=PA63&lpg=PA63
 * @note must run at (ideally) fixed intervals
 * @todo implement variable building tau based on e.g. occupancy/time of day: lower when window/doors can be opened
 * @param bmodel target building model
 */
static void bmodel_outdoor(struct s_bmodel * const bmodel)
{
	time_t now, dt;

	assert(bmodel);	// guaranteed to be called with bmodel configured

	bmodel_outdoor_temp(bmodel);

	now = bmodel->run.t_out_ltime;	// what matters is the actual update time of the outdoor sensor
	dt = now - bmodel->run.t_out_faltime;

	if (dt >= OUTDOOR_AVG_UPDATE_DT) {
		bmodel->run.t_out_faltime = now;

		bmodel->run.t_out_filt = temp_expw_mavg(bmodel->run.t_out_filt, bmodel->run.t_out, bmodel->set.tau, dt);
		bmodel->run.t_out_att = temp_expw_mavg(bmodel->run.t_out_att, bmodel->run.t_out_filt, bmodel->set.tau, dt);

		bmodel_save(bmodel);
	}

	// calculate mixed temp last: makes it work at init
	bmodel->run.t_out_mix = (bmodel->run.t_out + bmodel->run.t_out_filt)/2;	// XXX other possible calculation: X% of t_outdoor + 1-X% of t_filtered. Current setup is 50%

	dbgmsg("\"%s\": t_out: %.1f, t_filt: %.1f, t_outmixed: %.1f, t_outatt: %.1f", bmodel->name,
	       temp_to_celsius(bmodel->run.t_out),
	       temp_to_celsius(bmodel->run.t_out_filt),
	       temp_to_celsius(bmodel->run.t_out_mix),
	       temp_to_celsius(bmodel->run.t_out_att));
}

/**
 * Conditions for building summer switch.
 * summer mode is set on in ALL of the following conditions are met:
 * - t_outdoor_60 > limit_tsummer
 * - t_out_mix > limit_tsummer
 * - t_out_att > limit_tsummer
 * summer mode is back off if ALL of the following conditions are met:
 * - t_outdoor_60 < limit_tsummer
 * - t_out_mix < limit_tsummer
 * - t_out_att < limit_tsummer
 * State is preserved in all other cases
 * @note because we use AND, there's no need for hysteresis
 * @param bmodel target building model
 * @return exec status
 */
static int bmodel_summer(struct s_bmodel * const bmodel)
{
	const struct s_runtime * restrict const runtime = get_runtime();

	assert(bmodel);	// guaranteed to be called with bmodel configured

	if (!runtime->config->limit_tsummer) {
		bmodel->run.summer = false;
		return (-ENOTCONFIGURED);	// invalid limit, stop here
	}

	if ((bmodel->run.t_out > runtime->config->limit_tsummer)	&&
	    (bmodel->run.t_out_mix > runtime->config->limit_tsummer)	&&
	    (bmodel->run.t_out_att > runtime->config->limit_tsummer)) {
		bmodel->run.summer = true;
	}
	else {
		if ((bmodel->run.t_out < runtime->config->limit_tsummer)	&&
		    (bmodel->run.t_out_mix < runtime->config->limit_tsummer)	&&
		    (bmodel->run.t_out_att < runtime->config->limit_tsummer))
			bmodel->run.summer = false;
	}

	return (ALL_OK);
}

/**
 * Conditions for frost switch.
 * Trigger frost protection flag when t_outdoor_60 < limit_tfrost.
 * @note there is a fixed 1K positive hysteresis (on untrip)
 * @warning if limit_tfrost isn't available, frost is @b disabled.
 * @param bmodel target building model
 * @return exec status
 */
static int bmodel_frost(struct s_bmodel * restrict const bmodel)
{
	const struct s_runtime * restrict const runtime = get_runtime();

	assert(bmodel);	// guaranteed to be called with bmodel configured

	if (!runtime->config->limit_tfrost) {
		bmodel->run.frost = false;
		return (-ENOTCONFIGURED);	// invalid limit, stop here
	}

	if ((bmodel->run.t_out < runtime->config->limit_tfrost))
		bmodel->run.frost = true;
	else if ((bmodel->run.t_out > (runtime->config->limit_tfrost + deltaK_to_temp(1))))
		bmodel->run.frost = false;

	return (ALL_OK);
}

/**
 * Restore all models.
 * @param models list of models to restore
 */
static void models_restore(struct s_models * restrict const models)
{
	struct s_bmodel_l * restrict bmodelelmt;

	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		bmodel_restore(bmodelelmt->bmodel);
}

/**
 * Save all models.
 * @param models list of models to save
 */
static void models_save(const struct s_models * restrict const models)
{
	struct s_bmodel_l * restrict bmodelelmt;

	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		bmodel_save(bmodelelmt->bmodel);
}

/**
 * Create a new building model and attach it to the list of models.
 * @param models the list of models
 * @param name the model name, @b MUST be unique. A local copy is created
 * @return an allocated building model structure or NULL if failed.
 */
struct s_bmodel * models_new_bmodel(struct s_models * restrict const models, const char * restrict const name)
{
	struct s_bmodel * restrict bmodel = NULL;
	struct s_bmodel_l * restrict bmodelelmt = NULL;
	char * restrict str = NULL;

	if (!models || !name)
		goto fail;

	// ensure unique name
	if (bmodels_fbn(models->bmodels, name))
		goto fail;

	str = strdup(name);
	if (!str)
		goto fail;

	// create a new bmodel
	bmodel = calloc(1, sizeof(*bmodel));
	if (!bmodel)
		goto fail;

	// set name
	bmodel->name = str;

	// create a bmodel list element
	bmodelelmt = calloc(1, sizeof(*bmodelelmt));
	if (!bmodelelmt)
		goto fail;

	// attach created bmodel to element
	bmodelelmt->bmodel = bmodel;

	// insert the element in the models list
	bmodelelmt->id = models->bmodels_n;
	bmodelelmt->next = models->bmodels;
	models->bmodels = bmodelelmt;
	models->bmodels_n++;

	return (bmodel);

fail:
	free(str);
	free(bmodel);
	free(bmodelelmt);
	return (NULL);
}

/**
 * Allocate a new list of models.
 * @return an allocated list of models or NULL.
 */
struct s_models * models_new(void)
{
	struct s_models * const models = calloc(1, sizeof(*models));

	return (models);
}

/**
 * Delete a list of models.
 * @param models the list to delete
 */
void models_del(struct s_models * models)
{
	struct s_bmodel_l * bmodelelmt, * bmodelnext;

	if (!models)
		return;

	// clear all bmodels
	bmodelelmt = models->bmodels;
	while (bmodelelmt) {
		bmodelnext = bmodelelmt->next;
		bmodel_del(bmodelelmt->bmodel);
		free(bmodelelmt);
		models->bmodels_n--;
		bmodelelmt = bmodelnext;
	}

	// free resources
	free(models);
}

/**
 * Bring models online.
 * @param models target models
 * @return exec status
 */
int models_online(struct s_models * restrict const models)
{
	if (!models)
		return (-EINVALID);

	models_restore(models);

	models->online = true;

	return (ALL_OK);
}

/**
 * Take models offline.
 * @param models target models
 * @return exec status
 */
int models_offline(struct s_models * restrict const models)
{
	if (!models)
		return (-EINVALID);

	models_save(models);

	models->online = false;

	return (ALL_OK);
}

/**
 * Run all models.
 * @param models the list of models
 * @return exec status
 */
int models_run(struct s_models * restrict const models)
{
	struct s_bmodel_l * restrict bmodelelmt;

	if (!models)
		return (-EINVALID);

	if (!models->configured)
		return (-ENOTCONFIGURED);

	if (!models->online)
		return (-EOFFLINE);

	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		bmodel_outdoor(bmodelelmt->bmodel);
		bmodel_summer(bmodelelmt->bmodel);
		bmodel_frost(bmodelelmt->bmodel);
	}

	return (ALL_OK);
}

/**
 * Parse building models for summer switch evaluation. Conditions:
 * - If something's wrong, summer mode is unset.
 * - If @b ALL configured bmodels are compatible with summer mode, summer mode is set.
 * - If @b ANY configured bmodel is incompatible with summer mode, summer mode is unset.
 * @param models model list from which to process the building models
 * @return summer mode
 */
bool models_summer(const struct s_models * restrict const models)
{
	struct s_bmodel_l * bmodelelmt;
	bool summer = true;

	// if something isn't quite right, return false by default
	if (!models || !models->configured || !models->online)
		return (false);

	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		summer &= bmodelelmt->bmodel->run.summer;
	}

	return (summer);
}
