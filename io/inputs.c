//
//  io/inputs.c
//  rwchcd
//
//  (C) 2020,2023 Thibaut VARENE
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
 *   - Switches
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rwchcd.h"
#include "inputs/temperature.h"
#include "inputs/switch.h"
#include "inputs.h"
#include "log/log.h"
#include "lib.h"

struct s_inputs Inputs;
static struct s_log_source In_temps_lsrc;

// Workaround to disambiguate 0 inid
#define inputs_inid_to_id(x)	((typeof(x))(x-1))
#define inputs_id_to_inid(x)	((typeof(x))(x+1))

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
	freeconst(In_temps_lsrc.keys);
	freeconst(In_temps_lsrc.metrics);

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
 * Find an input by name.
 * @param t the type of input to look for
 * @param name the unique name to look for
 * @return the input id or error status
 */
int inputs_fbn(const enum e_input_type t, const char * name)
{
	inid_t id;
	int ret = -ENOTFOUND;

	if (!name)
		return (-EINVALID);

	switch (t) {
		case INPUT_TEMP:
			for (id = 0; id < Inputs.temps.last; id++) {
				if (!strcmp(Inputs.temps.all[id].name, name)) {
					ret = (int)inputs_id_to_inid(id);
					break;
				}
			}
			break;
		case INPUT_SWITCH:
			for (id = 0; id < Inputs.switches.last; id++) {
				if (!strcmp(Inputs.switches.all[id].name, name)) {
					ret = (int)inputs_id_to_inid(id);
					break;
				}
			}
			break;
		case INPUT_NONE:
		default:
			break;
	}
	return (ret);
}

/**
 * Return an input name.
 * @param t the type of input to look for
 * @param inid the input id
 * @return input name or NULL on error
 */
const char * inputs_name(const enum e_input_type t, const inid_t inid)
{
	const inid_t id = inputs_inid_to_id(inid);
	const char * name = NULL;

	switch (t) {
		case INPUT_TEMP:
			if (likely(id < Inputs.temps.last))
				name = Inputs.temps.all[id].name;
			break;
		case INPUT_SWITCH:
			if (likely(id < Inputs.switches.last))
				name = Inputs.switches.all[id].name;
			break;
		case INPUT_NONE:
		default:
			break;
	}

	return (name);
}

/**
 * Get an input value.
 * @param t the type of input to look for
 * @param inid the input id to read from
 * @param valout an optional pointer to suitable memory area to store the result
 * @return exec status
 */
int inputs_get(const enum e_input_type t, const inid_t inid, void * const valout)
{
	const inid_t id = inputs_inid_to_id(inid);
	int ret = -EINVALID;

	switch (t) {
		case INPUT_TEMP:
			if (likely(id < Inputs.temps.last))
				ret = temperature_get(&Inputs.temps.all[id], valout);
			break;
		case INPUT_SWITCH:
			if (likely(id < Inputs.switches.last))
				ret = switch_get(&Inputs.switches.all[id], valout);
			break;
		case INPUT_NONE:
		default:
			break;

	}

	return (ret);
}

/**
 * Get an input last update time.
 * @param t the type of input to look for
 * @param inid the input id to read from
 * @param stamp an optional pointer to store the result
 * @return exec status
 * @note this function will @b not request an update of the underlying input
 * @note the underlying plumbing is not implemented for all input types.
 */
int inputs_time(const enum e_input_type t, const inid_t inid, timekeep_t * const stamp)
{
	const inid_t id = inputs_inid_to_id(inid);
	int ret = -EINVALID;

	switch (t) {
		case INPUT_TEMP:
			if (likely(id < Inputs.temps.last))
				ret = temperature_time(&Inputs.temps.all[id], stamp);
			break;
		case INPUT_SWITCH:
			ret = -ENOTIMPLEMENTED;
			break;
		case INPUT_NONE:
		default:
			break;
	}

	return (ret);
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
	inid_t id;

	// clean all registered temps
	for (id = 0; id < Inputs.temps.last; id++)
		temperature_clear(&Inputs.temps.all[id]);

	// clean all registered switches
	for (id = 0; id < Inputs.switches.last; id++)
		switch_clear(&Inputs.switches.all[id]);

	freeconst(Inputs.temps.all);
	freeconst(Inputs.switches.all);

	memset(&Inputs, 0x00, sizeof(Inputs));
}
