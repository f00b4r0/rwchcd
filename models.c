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
 * This file currently implements basic building models based on time constant.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "lib.h"
#include "storage.h"
#include "io/inputs.h"
#include "alarms.h"
#include "models.h"
#include "log/log.h"

#define OUTDOOR_SMOOTH_TIME		(60*TIMEKEEP_SMULT)	///< time in seconds over which outdoor temp is smoothed
#define OUTDOOR_AVG_UPDATE_DT		(600*TIMEKEEP_SMULT)	///< prevents running averages at less than 10mn interval. Should be good up to 100h tau.
#define MODELS_STORAGE_BMODEL_PREFIX	"models_bmodel"

struct s_models Models;	///< Known models

static const storage_version_t Models_sversion = 5;

/**
 * Building model data log callback.
 * @warning uses statically allocated data, must not be called concurrently.
 * @param ldata the log data to populate
 * @param object the opaque pointer to bmodel structure
 * @return exec status
 */
static int bmodel_logdata_cb(struct s_log_data * const ldata, const void * const object)
{
	const struct s_bmodel * const bmodel = object;
	unsigned int i = 0;

	assert(ldata);
	assert(ldata->nkeys >= 6);

	if (!bmodel)
		return (-EINVALID);

	if (!bmodel->run.online)
		return (-EOFFLINE);

	ldata->values[i++] = bmodel->run.summer;
	ldata->values[i++] = bmodel->run.frost;
	ldata->values[i++] = bmodel->run.t_out;
	ldata->values[i++] = bmodel->run.t_out_filt;
	ldata->values[i++] = bmodel->run.t_out_mix;
	ldata->values[i++] = bmodel->run.t_out_att;

	ldata->nvalues = i;

	return (ALL_OK);
}

/**
 * Provide a well formatted log source for a given building model.
 * @param bmodel the target bmodel
 * @return (statically allocated) s_log_source pointer
 * @warning must not be called concurrently
 */
static const struct s_log_source * bmodel_lreg(const struct s_bmodel * const bmodel)
{
	static const log_key_t keys[] = {
		"summer", "frost", "t_out", "t_out_filt", "t_out_mix", "t_out_att",
	};
	static const enum e_log_metric metrics[] = {
		LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE, LOG_METRIC_GAUGE,
	};
	const log_version_t version = 1;
	static struct s_log_source Bmodel_lreg;

	Bmodel_lreg = (struct s_log_source){
		.log_sched = LOG_SCHED_15mn,
		.basename = MODELS_STORAGE_BMODEL_PREFIX,
		.identifier = bmodel->name,
		.version = version,
		.nkeys = ARRAY_SIZE(keys),
		.keys = keys,
		.metrics = metrics,
		.logdata_cb = bmodel_logdata_cb,
		.object = bmodel,
	};
	return (&Bmodel_lreg);
}

/**
 * Register a building model for logging.
 * @param bmodel the target bmodel
 * @return exec status
 */
static int bmodel_log_register(const struct s_bmodel * const bmodel)
{
	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	if (!bmodel->set.logging)
		return (ALL_OK);

	return (log_register(bmodel_lreg(bmodel)));
}

/**
 * Deregister a building model from logging.
 * @param bmodel the target bmodel
 * @return exec status
 */
static int bmodel_log_deregister(const struct s_bmodel * const bmodel)
{
	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	if (!bmodel->set.logging)
		return (ALL_OK);

	return (log_deregister(bmodel_lreg(bmodel)));
}

/**
 * Save building model state to permanent storage.
 * @param bmodel the building model to save, @b MUST be named
 * @return exec status
 */
static int bmodel_save(const struct s_bmodel * restrict const bmodel)
{
	char buf[MAX_FILENAMELEN+1] = MODELS_STORAGE_BMODEL_PREFIX;

	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	// can't store if no name
	if (!bmodel->name)
		return (-EINVALID);

	strcat(buf, "_");
	strncat(buf, bmodel->name, MAX_FILENAMELEN-strlen(buf)-1);

	return (storage_dump(buf, &Models_sversion, &bmodel->run, sizeof(bmodel->run)));
}

/**
 * Restore building model state from permanent storage.
 * @param bmodel the building model to restore, @b MUST be named
 * @return exec status
 */
static int bmodel_restore(struct s_bmodel * restrict const bmodel)
{
	char buf[MAX_FILENAMELEN+1] = MODELS_STORAGE_BMODEL_PREFIX;
	struct s_bmodel temp_bmodel;
	storage_version_t sversion;
	int ret;

	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	// can't restore if no name
	if (!bmodel->name)
		return (-EINVALID);

	strcat(buf, "_");
	strncat(buf, bmodel->name, MAX_FILENAMELEN-strlen(buf)-1);

	// try to restore key elements
	ret = storage_fetch(buf, &sversion, &temp_bmodel.run, sizeof(temp_bmodel.run));
	if (ALL_OK == ret) {
		if (Models_sversion != sversion)
			return (-EMISMATCH);

		bmodel->run.summer = temp_bmodel.run.summer;
		bmodel->run.frost = temp_bmodel.run.frost;
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

	assert(name);

	for (bml = bmodels; bml; bml = bml->next) {
		if (!strcmp(bml->bmodel->name, name)) {
			bmodel = bml->bmodel;
			break;
		}
	}

	return (bmodel);
}

/**
 * Bring a building model online.
 * Checks that outdoor sensor is available
 * @param bmodel target bmodel
 * @return exec status
 */
static int bmodel_online(struct s_bmodel * restrict const bmodel)
{
	int ret;
	temp_t tout;

	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	if (validate_temp(bmodel->set.limit_tsummer) != ALL_OK)
		return (-EMISCONFIGURED);

	if (validate_temp(bmodel->set.limit_tfrost) != ALL_OK)
		return (-EMISCONFIGURED);

	if (bmodel->set.tau <= 0) {
		pr_err(_("Building model \"%s\": invalid value for tau: '%ld'"), bmodel->name, timekeep_tk_to_sec(bmodel->set.tau));
		return (-EMISCONFIGURED);
	}

	// make sure specified outdoor sensor is available
	ret = inputs_temperature_get(bmodel->set.tid_outdoor, &tout);
	if (ALL_OK != ret) {
		pr_err(_("Building model \"%s\": outdoor sensor error (%d)"), bmodel->name, ret);
		return (ret);
	}

	bmodel->run.t_out = tout;
	inputs_temperature_time(bmodel->set.tid_outdoor, &bmodel->run.t_out_ltime);

	// set sane values if necessary
	if (!bmodel->run.t_out_att || !bmodel->run.t_out_filt) {
		bmodel->run.t_out_att = bmodel->run.t_out_filt = tout;
	}
	// force set t_out_faltime since we can't restore it
	inputs_temperature_time(bmodel->set.tid_outdoor, &bmodel->run.t_out_faltime);

	// log registration shouldn't cause online failure
	if (bmodel_log_register(bmodel) != ALL_OK)
		pr_err(_("Building model \"%s\": couldn't register for logging"), bmodel->name);

	bmodel->run.online = true;

	return (ALL_OK);
}

/**
 * Take a building model offline.
 * @param bmodel target bmodel
 * @return exec status
 */
static int bmodel_offline(struct s_bmodel * restrict const bmodel)
{
	assert(bmodel);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	bmodel_log_deregister(bmodel);

	bmodel->run.online = false;

	return (ALL_OK);
}

/**
 * Delete a building model.
 * @param bmodel target model
 */
static void bmodel_del(struct s_bmodel * restrict bmodel)
{
	if (!bmodel)
		return;

	free((void *)bmodel->name);
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
	const timekeep_t last = bmodel->run.t_out_ltime;	// previous sensor time
	timekeep_t dt;
	temp_t toutdoor;
	int ret;

	ret = inputs_temperature_get(bmodel->set.tid_outdoor, &toutdoor);
	if (likely(ALL_OK == ret)) {
		inputs_temperature_time(bmodel->set.tid_outdoor, &bmodel->run.t_out_ltime);
		dt = bmodel->run.t_out_ltime - last;
		bmodel->run.t_out = temp_expw_mavg(bmodel->run.t_out, toutdoor, OUTDOOR_SMOOTH_TIME, dt);
	}
	else {
		// in case of outdoor sensor failure, assume outdoor temp is tfrost-1: ensures frost protection
		bmodel->run.t_out = bmodel->set.limit_tfrost-1;
		alarms_raise(ret, _("Outdoor sensor failure"), _("Outdr sens fail"));
	}
}

/**
 * Process the building model outdoor temperature.
 * Compute the values of mixed and attenuated outdoor temp based on a
 * weighted moving average and the building time constant.
 * This function is designed so that at init time, when the variables are all 0,
 * the averages will take the value of the current outdoor temperature.
 * - http://liu.diva-portal.org/smash/get/diva2:893577/FULLTEXT01.pdf
 * - http://www.ibpsa.org/proceedings/BS2013/p_2030.pdf
 * - http://www.wseas.us/e-library/conferences/2013/Brasov/ACMOS/ACMOS-32.pdf
 * - http://www.emu.systems/en/blog/2015/10/19/whats-the-time-constant-of-a-building
 * - https://books.google.fr/books?id=dIYxQkS_SWMC&pg=PA63&lpg=PA63
 *
 * @param bmodel target building model
 * @return exec status
 * @note must run at (ideally) fixed intervals
 * @todo implement variable building tau based on e.g. occupancy/time of day: lower when window/doors can be opened
 */
static int bmodel_outdoor(struct s_bmodel * const bmodel)
{
	timekeep_t now, dt;

	assert(bmodel);	// guaranteed to be called with bmodel configured

	if (unlikely(!bmodel->run.online))	// but not necessarily online
		return (-EOFFLINE);

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

	dbgmsg(1, 1, "\"%s\": t_out: %.1f, t_filt: %.1f, t_mix: %.1f, t_att: %.1f", bmodel->name,
	       temp_to_celsius(bmodel->run.t_out),
	       temp_to_celsius(bmodel->run.t_out_filt),
	       temp_to_celsius(bmodel->run.t_out_mix),
	       temp_to_celsius(bmodel->run.t_out_att));

	return (ALL_OK);
}

/**
 * Conditions for building summer switch.
 * summer mode is set on in @b ALL of the following conditions are met:
 * - t_outdoor_60 > limit_tsummer
 * - t_out_mix > limit_tsummer
 * - t_out_att > limit_tsummer
 *
 * summer mode is back off if @b ALL of the following conditions are met:
 * - t_outdoor_60 < limit_tsummer
 * - t_out_mix < limit_tsummer
 * - t_out_att < limit_tsummer
 *
 * State is preserved in all other cases
 * @note because we use AND, there's no need for hysteresis
 * @param bmodel target building model
 * @return exec status
 */
static int bmodel_summer(struct s_bmodel * const bmodel)
{
	assert(bmodel);	// guaranteed to be called with bmodel configured

	if (unlikely(!bmodel->run.online))	// but not necessarily online
		return (-EOFFLINE);

	if (unlikely(!bmodel->set.limit_tsummer)) {
		bmodel->run.summer = false;
		return (-EMISCONFIGURED);	// invalid limit, stop here
	}

	if ((bmodel->run.t_out > bmodel->set.limit_tsummer)	&&
	    (bmodel->run.t_out_mix > bmodel->set.limit_tsummer)	&&
	    (bmodel->run.t_out_att > bmodel->set.limit_tsummer)) {
		bmodel->run.summer = true;
	}
	else {
		if ((bmodel->run.t_out < bmodel->set.limit_tsummer)	&&
		    (bmodel->run.t_out_mix < bmodel->set.limit_tsummer)	&&
		    (bmodel->run.t_out_att < bmodel->set.limit_tsummer))
			bmodel->run.summer = false;
	}

	return (ALL_OK);
}

/**
 * Conditions for frost switch.
 * Trigger frost protection flag when t_outdoor_60 < limit_tfrost.
 * @note there is a fixed 1K positive hysteresis (on untrip)
 * @warning if limit_tfrost isn't available, frost protection is @b disabled.
 * @param bmodel target building model
 * @return exec status
 */
static int bmodel_frost(struct s_bmodel * restrict const bmodel)
{
	assert(bmodel);	// guaranteed to be called with bmodel configured

	if (unlikely(!bmodel->run.online))	// but not necessarily online
		return (-EOFFLINE);

	if (unlikely(!bmodel->set.limit_tfrost)) {
		bmodel->run.frost = false;
		return (-EMISCONFIGURED);	// invalid limit, stop here
	}

	if ((bmodel->run.t_out < bmodel->set.limit_tfrost)) {
		bmodel->run.frost = true;
		bmodel->run.summer = false;	// override summer
	}
	else if ((bmodel->run.t_out > (bmodel->set.limit_tfrost + deltaK_to_temp(1))))
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

	assert(models);

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

	assert(models);

	for (bmodelelmt = models->bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		bmodel_save(bmodelelmt->bmodel);
}

/**
 * Create a new building model and attach it to the list of models.
 * @note a name length can work in this function but be too long for logging due to prefix/suffix.
 * @param name the model name, @b MUST be unique. A local copy is created
 * @return an allocated building model structure or NULL if failed.
 */
struct s_bmodel * models_new_bmodel(const char * restrict const name)
{
	struct s_bmodel * restrict bmodel = NULL;
	struct s_bmodel_l * restrict bmodelelmt = NULL;
	char * restrict str = NULL;

	if (!name)
		goto fail;

	// ensure name is short enough
	if ((strlen(MODELS_STORAGE_BMODEL_PREFIX) + 1 + strlen(name) + 1) >= MAX_FILENAMELEN) {
		pr_err(_("Name too long: \"%s\" (max: %d chars)"), name, (MAX_FILENAMELEN - 1 - 1 - strlen(MODELS_STORAGE_BMODEL_PREFIX)));
		goto fail;
	}

	// ensure unique name
	if (bmodels_fbn(Models.bmodels, name))
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
	bmodelelmt->id = Models.bmodels_n;
	bmodelelmt->next = Models.bmodels;
	Models.bmodels = bmodelelmt;
	Models.bmodels_n++;

	return (bmodel);

fail:
	free(str);
	free(bmodel);
	free(bmodelelmt);
	return (NULL);
}

/**
 * Find a building model by name from models.
 * @param name target name to find
 * @return bmodel if found, NULL otherwise
 */
const struct s_bmodel * models_fbn_bmodel(const char * restrict const name)
{
	if (!name)
		return (NULL);

	return (bmodels_fbn(Models.bmodels, name));
}

/**
 * Initialize the models subsystem.
 * @return exec status
 */
int models_init(void)
{
	memset(&Models, 0x00, sizeof(Models));

	return (ALL_OK);
}

/**
 * Cleanup the models subsystem.
 */
void models_exit(void)
{
	struct s_bmodel_l * bmodelelmt, * bmodelnext;

	// clear all bmodels
	bmodelelmt = Models.bmodels;
	while (bmodelelmt) {
		bmodelnext = bmodelelmt->next;
		bmodel_del(bmodelelmt->bmodel);
		free(bmodelelmt);
		Models.bmodels_n--;
		bmodelelmt = bmodelnext;
	}
}

/**
 * Bring models online.
 * @return exec status
 */
int models_online(void)
{
	struct s_bmodel_l * restrict bmodelelmt;
	int ret;

	models_restore(&Models);

	// bring building models online
	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		ret = bmodel_online(bmodelelmt->bmodel);
		if (ALL_OK != ret)
			return (ret);
	}

	Models.online = true;

	return (ALL_OK);
}

/**
 * Take models offline.
 * @return exec status
 */
int models_offline(void)
{
	struct s_bmodel_l * restrict bmodelelmt;

	models_save(&Models);

	// take building models offline
	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		bmodel_offline(bmodelelmt->bmodel);

	Models.online = false;

	return (ALL_OK);
}

/**
 * Run all models.
 * @return exec status
 */
int models_run(void)
{
	struct s_bmodel_l * restrict bmodelelmt;

	if (unlikely(!Models.online))
		return (-EOFFLINE);

	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (unlikely(!bmodelelmt->bmodel->set.configured))
			continue;
		bmodel_outdoor(bmodelelmt->bmodel);
		bmodel_summer(bmodelelmt->bmodel);
		bmodel_frost(bmodelelmt->bmodel);
	}

	return (ALL_OK);
}

/** @deprecated quick temporary hack for backward compatibility */
temp_t models_outtemp(void)
{
	struct s_bmodel_l * bmodelelmt;
	temp_t temp = 0;

	// if something isn't quite right, return error by default
	if (!Models.online)
		return (-EOFFLINE);

	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next)
		temp += bmodelelmt->bmodel->run.t_out;

	temp /= Models.bmodels_n;	// average

	return (temp);
}
