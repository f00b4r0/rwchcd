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
#include <string.h>	// memset
#include <assert.h>

#include "heatsource.h"

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
 * Run heatsource.
 * @note Honoring runmode is left to private routines
 * @param heat target heatsource
 * @return exec status
 */
int heatsource_run(struct s_heatsource * const heat)
{
	if (!heat)
		return (-EINVALID);

	if (!heat->run.online)	// implies set.configured == true
		return (-EOFFLINE);

	if (heat->cb.run)
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

	free(heat->name);
	heat->name = NULL;

	free(heat);
}
