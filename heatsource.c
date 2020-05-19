//
//  heatsource.c
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heatsource operation implementation.
 *
 * The heatsource implementation supports:
 * - Overtemp signaling (to trigger maximum dissipation via connected consumers)
 * - Consumer shift (e.g. to accelerate warmup after a cold start or to evacuate excess heat)
 * - Consumer reduction delay signal (signal consumers to delay heat request reduction)
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "heatsource.h"
#include "runtime.h"
#include "scheduler.h"

/**
 * Create a heatsource
 * @return the newly created heatsource or NULL
 */
struct s_heatsource * heatsource_new(void)
{
	struct s_heatsource * const heatsource = calloc(1, sizeof(*heatsource));
	return (heatsource);
}

/**
 * Put heatsource online.
 * Perform all necessary actions to prepare the heatsource for service and
 * mark it as online.
 * @param heat target heatsource
 * @return exec status
 */
int heatsource_online(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if ((HS_NONE == heat->set.type) || (HS_UNKNOWN <= heat->set.type)) {	// type HS_NONE or unknown, misconfiguration
		pr_err(_("\"%s\": invalid heatsource type (%d)"), heat->name, heat->set.type);
		return (-EMISCONFIGURED);
	}

	// check we have a priv element
	if (!heat->priv) {
		pr_err(_("\"%s\": missing private data)"), heat->name);
		return (-EMISCONFIGURED);
	}

	if (heat->cb.online)
		ret = heat->cb.online(heat);

	if (ALL_OK == ret)
		heat->run.online = true;

	return (ret);
}

/**
 * Put heatsource offline.
 * Perform all necessary actions to completely shut down the heatsource and
 * mark it as offline.
 * @param heat target heatsource
 * @return exec status
 */
int heatsource_offline(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (heat->cb.offline)
		ret = heat->cb.offline(heat);

	// reset runtime data (resets online status)
	memset(&heat->run, 0x0, sizeof(heat->run));

	return (ret);
}

/**
 * Heat source logic.
 * @param heat target heat source
 * @return exec status
 * @todo rework DHWT prio when n_heatsources > 1
 */
__attribute__((warn_unused_result))
static int heatsource_logic(struct s_heatsource * restrict const heat)
{
	const struct s_runtime * restrict const runtime = runtime_get();
	const struct s_schedule_eparams * eparams;
	const timekeep_t now = timekeep_now();
	const timekeep_t dt = now - heat->run.last_run_time;
	temp_t temp;
	int ret = -ENOTIMPLEMENTED;

	assert(runtime);

	// handle global/local runmodes
	if (RM_AUTO == heat->set.runmode) {
		// if we have a schedule, use it, or global settings if unavailable
		eparams = scheduler_get_schedparams(heat->set.schedid);
		heat->run.runmode = ((SYS_AUTO == runtime->systemmode) && eparams) ? eparams->runmode : runtime->runmode;
	}
	else
		heat->run.runmode = heat->set.runmode;

	heat->run.could_sleep = heat->pdata->plant_could_sleep;	// XXX

	// compute sliding integral in DHW sliding prio
	// XXX TODO: this logic should move at a higher level in the context of a pool of heatsources (some of which may or may not be connected to the DHWTs)
	if (heat->pdata->dhwc_sliding) {
		// jacket integral between -100Ks and 0
		temp = temp_thrs_intg(&heat->run.sld_itg, heat->run.temp_request, heat->cb.temp(heat), heat->cb.time(heat), (signed)timekeep_sec_to_tk(deltaK_to_temp(-100)), 0);
		// percentage of shift is formed by the integral of current temp vs expected temp: 1Ks is -1% shift
		heat->run.cshift_noncrit = timekeep_tk_to_sec(temp_to_ikelvind(temp));
	}
	else
		reset_intg(&heat->run.sld_itg);

	// decrement consummer stop delay if any
	if (dt < heat->run.target_consumer_sdelay)
		heat->run.target_consumer_sdelay -= dt;
	else
		heat->run.target_consumer_sdelay = 0;

	if (heat->cb.logic)
		ret = heat->cb.logic(heat);

	heat->run.last_run_time = now;

	return (ret);
}

/**
 * Run heatsource.
 * @note Honoring runmode is left to private routines
 * @param heat target heatsource
 * @return exec status
 */
int heatsource_run(struct s_heatsource * const heat)
{
	int ret;

	if (unlikely(!heat))
		return (-EINVALID);

	if (unlikely(!heat->run.online))	// implies set.configured == true
		return (-EOFFLINE);

	ret = heatsource_logic(heat);
	if (unlikely(ALL_OK != ret))
		return (ret);

	if (likely(heat->cb.run))
		return (heat->cb.run(heat));
	else
		return (-ENOTIMPLEMENTED);
}

/**
 * Delete a heatsource.
 * @param heat the source to delete
 */
void heatsource_del(struct s_heatsource * heat)
{
	if (!heat)
		return;

	if (heat->cb.del_priv)
		heat->cb.del_priv(heat->priv);
	heat->priv = NULL;

	free((void *)heat->name);
	heat->name = NULL;

	free(heat);
}
