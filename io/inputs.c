//
//  io/inputs.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global inputs interface implementation.
 *
 * This subsystem interfaces between the hardware backends and the data consumers. No consumer should ever directly address the backends,
 * instead they should use this interface.
 *
 * The inputs implementation supports:
 * - Logging of all input values
 * - Virtually unlimited number of inputs, of various types:
 *   - Temperatures
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rwchcd.h"
#include "inputs/temperature.h"
#include "inputs.h"
#include "log/log.h"
#include "lib.h"

struct s_inputs Inputs;
static struct s_log_source In_temps_lsrc;

// Workaround to disambiguate 0 itid
#define inputs_itid_to_id(x)	((typeof(x))(x-1))
#define inputs_id_to_itid(x)	((typeof(x))(x+1))

/**
 * Temperatures data log callback.
 * This function logs known temperature in rounded integer Kelvin.
 * @param ldata the log data to populate
 * @param object unused
 * @return exec status
 */
static int temps_logdata_cb(struct s_log_data * const ldata, const void * const object __attribute__((unused)))
{
	unsigned int id = 0;
	temp_t temp;
	int ret;

	assert(ldata);
	assert(ldata->nkeys >= Inputs.temps.last);

	for (id = 0; id < Inputs.temps.last; id++) {
		ret = temperature_get(&Inputs.temps.all[id], &temp);
		if (ALL_OK == ret)
			ldata->values[id].f = temp_to_celsius(temp);
		else
			ldata->values[id].f = 0;
	}

	ldata->nvalues = id;

	return (ALL_OK);
}

/**
 * Register inputs for logging.
 * @return exec status
 */
static int inputs_log_register(void)
{
	const unsigned int nmemb = Inputs.temps.last;
	log_key_t *keys;
	enum e_log_metric *metrics;
	unsigned int id;

	if (!nmemb)
		return (ALL_OK);

	keys = calloc(nmemb, sizeof(*keys));
	if (!keys)
		return (-EOOM);

	for (id = 0; id < nmemb; id++)
		keys[id] = Inputs.temps.all[id].name;

	metrics = calloc(nmemb, sizeof(*metrics));
	if (!metrics) {
		free(keys);
		return (-EOOM);
	}

	for (id = 0; id < nmemb; id++)
		metrics[id] = LOG_METRIC_FGAUGE;

	In_temps_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_10s,
		.basename = "inputs",
		.identifier = "temperatures",
		.version = 1,
		.logdata_cb = temps_logdata_cb,
		.nkeys = nmemb,
		.keys = keys,
		.metrics = metrics,
		.object = NULL,
	};

	return (log_register(&In_temps_lsrc));
}

/**
 * Deregister inputs from logging.
 * @return exec status
 */
static int inputs_log_deregister(void)
{
	int ret;

	ret = log_deregister(&In_temps_lsrc);
	if (ret) {
		dbgerr("log_deregister failed (%d)", ret);
	}
	free((void *)In_temps_lsrc.keys);
	free((void *)In_temps_lsrc.metrics);

	return (ret);
}

/**
 * Init inputs system.
 * This function clears internal  state.
 */
int inputs_init(void)
{
	memset(&Inputs, 0x00, sizeof(Inputs));

	return (ALL_OK);
}

/**
 * Online inputs.
 * Registers log.
 * @return exec status
 */
int inputs_online(void)
{
	int ret;

	ret = inputs_log_register();
	if (ret) {
		dbgerr("inputs_log_register failed (%d)", ret);
	}
	return (ALL_OK);
}

/**
 * Find a temperature input by name.
 * @param name the unique name to look for
 * @return the temperature input id or error status
 */
int inputs_temperature_fbn(const char * name)
{
	itid_t id;
	int ret = -ENOTFOUND;

	if (!name)
		return (-EINVALID);

	for (id = 0; id < Inputs.temps.last; id++) {
		if (!strcmp(Inputs.temps.all[id].name, name)) {
			ret = (int)inputs_id_to_itid(id);
			break;
		}
	}

	return (ret);
}

/**
 * Return a temperature input name.
 */
const char * inputs_temperature_name(const itid_t tid)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (NULL);

	return (Inputs.temps.all[id].name);
}

/**
 * Get a temperature input value.
 * @param tid the temperature input id to read from
 * @param tout an optional pointer to store the result
 * @return exec status
 */
int inputs_temperature_get(const itid_t tid, temp_t * const tout)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (-EINVALID);

	return (temperature_get(&Inputs.temps.all[id], tout));
}

/**
 * Get a temperature input last update time.
 * @param tid the temperature input id to read from
 * @param stamp an optional pointer to store the result
 * @return exec status
 * @note this function will @b not request an update of the underlying temperature
 */
int inputs_temperature_time(const itid_t tid, timekeep_t * const stamp)
{
	const itid_t id = inputs_itid_to_id(tid);

	if (unlikely(id >= Inputs.temps.last))
		return (-EINVALID);

	return (temperature_time(&Inputs.temps.all[id], stamp));
}

/**
 * Offline inputs.
 * Deregister log.
 * @return exec status
 */
int inputs_offline(void)
{
	inputs_log_deregister();
	return (ALL_OK);
}

/**
 * Cleanup inputs system
 */
void inputs_exit(void)
{
	itid_t id;

	// clean all registered temps
	for (id = 0; id < Inputs.temps.last; id++)
		temperature_clear(&Inputs.temps.all[id]);

	free((void *)Inputs.temps.all);
	memset(&Inputs, 0x00, sizeof(Inputs));
}
