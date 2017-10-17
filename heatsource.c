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
 */

#include <stdlib.h>	// calloc/free
#include <assert.h>

#include "heatsource.h"
#include "pump.h"
#include "valve.h"
#include "hardware.h"
#include "lib.h"

/**
 * Put heatsource online.
 * Perform all necessary actions to prepare the heatsource for service but
 * DO NOT MARK IT AS ONLINE.
 * @param heat target heatsource
 * @param return exec status
 */
int heatsource_online(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (HS_NONE == heat->set.type)	// type HS_NONE, misconfiguration
		return (-EMISCONFIGURED);

	// check we have a priv element
	if (!heat->priv)
		return (-EMISCONFIGURED);

	if (heat->hs_online)
		ret = heat->hs_online(heat);

	return (ret);
}

/**
 * Put heatsource offline.
 * Perform all necessary actions to completely shut down the heatsource but
 * DO NOT MARK IT AS OFFLINE.
 * @param heat target heatsource
 * @param return exec status
 */
int heatsource_offline(struct s_heatsource * const heat)
{
	int ret = -ENOTIMPLEMENTED;

	if (!heat)
		return (-EINVALID);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	heat->run.runmode = RM_OFF;

	if (heat->hs_offline)
		ret = heat->hs_offline(heat);

	return (ret);
}

/**
 * Run heatsource.
 * @note Honoring SYSMODE is left to private routines
 * @param heat target heatsource
 * @return exec status
 */
int heatsource_run(struct s_heatsource * const heat)
{
	assert(heat);

	if (!heat->set.configured)
		return (-ENOTCONFIGURED);

	if (!heat->run.online)
		return (-EOFFLINE);

	if (heat->hs_run)
		return (heat->hs_run(heat));
	else
		return (-ENOTIMPLEMENTED);
}

/**
 * Delete a heatsource
 * @param source the source to delete
 */
void heatsource_del(struct s_heatsource * heat)
{
	if (!heat)
		return;

	if (heat->hs_del_priv)
		heat->hs_del_priv(heat->priv);
	heat->priv = NULL;

	free(heat->name);
	heat->name = NULL;

	free(heat);
}
