//
//  io/outputs.c
//  rwchcd
//
//  (C) 2020-2021,2023 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Global outputs interface implementation.
 *
 * This subsystem interfaces between the hardware backends and the actuators requests. No request should ever directly address the backends,
 * instead they should use this interface,.
 *
 * The outputs implementation supports:
 * - Logging of output accounting
 * - Virtually unlimited number of outputs, of various types:
 *   - Relays
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "rwchcd.h"
#include "outputs/relay.h"
#include "outputs.h"
#include "log/log.h"

struct s_outputs Outputs;
static struct s_log_source Out_rcycles_lsrc;
static struct s_log_source Out_ronsecs_lsrc;

// Workaround to disambiguate 0 outid
#define outputs_outid_to_id(x)	((typeof(x))(x-1))
#define outputs_id_to_outid(x)	((typeof(x))(x+1))

/**
 * Relay cycles data log callback.
 * This function logs relay cycles.
 * @param ldata the log data to populate
 * @param object unused
 * @return exec status
 */
static int rcycles_logdata_cb(struct s_log_data * const ldata, const void * const object __attribute__((unused)))
{
	unsigned int id = 0;

	assert(ldata);
	assert(ldata->nkeys >= Outputs.relays.last);

	for (id = 0; id < Outputs.relays.last; id++)
		ldata->values[id].u = relay_acct_cycles_get(&Outputs.relays.all[id]);

	ldata->nvalues = id;

	return (ALL_OK);
}

/**
 * Relay on time data log callback.
 * This function logs relay on seconds.
 * @param ldata the log data to populate
 * @param object unused
 * @return exec status
 */
static int ronsecs_logdata_cb(struct s_log_data * const ldata, const void * const object __attribute__((unused)))
{
	unsigned int id = 0;

	assert(ldata);
	assert(ldata->nkeys >= Outputs.relays.last);

	for (id = 0; id < Outputs.relays.last; id++)
		ldata->values[id].u = relay_acct_ontotsec_get(&Outputs.relays.all[id]);

	ldata->nvalues = id;

	return (ALL_OK);
}

/**
 * Register outputs for logging.
 * @return exec status
 */
static int outputs_log_register(void)
{
	const unsigned int nmemb = Outputs.relays.last;
	log_key_t *keys;
	enum e_log_metric *metrics;
	unsigned int id;
	int ret;

	if (!nmemb)
		return (ALL_OK);

	keys = calloc(nmemb, sizeof(*keys));
	if (!keys)
		return (-EOOM);

	for (id = 0; id < nmemb; id++)
		keys[id] = Outputs.relays.all[id].name;

	metrics = calloc(nmemb, sizeof(*metrics));
	if (!metrics) {
		free(keys);
		return (-EOOM);
	}

	for (id = 0; id < nmemb; id++)
		metrics[id] = LOG_METRIC_ICOUNTER;

	Out_rcycles_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_15mn,
		.basename = "outputs",
		.identifier = "relays_cycles",
		.version = 1,
		.logdata_cb = rcycles_logdata_cb,
		.nkeys = nmemb,
		.keys = keys,
		.metrics = metrics,
		.object = NULL,
	};

	Out_ronsecs_lsrc = (struct s_log_source){
		.log_sched = LOG_SCHED_15mn,
		.basename = "outputs",
		.identifier = "relays_onsecs",
		.version = 1,
		.logdata_cb = ronsecs_logdata_cb,
		.nkeys = nmemb,
		// Note: reuse keys/metrics
		.keys = keys,
		.metrics = metrics,
		.object = NULL,
	};

	ret = log_register(&Out_rcycles_lsrc);
	if (ret) {
		dbgerr("log_register failed for Out_rcycles_lsrc (%d)", ret);
		goto cleanup;
	}

	ret = log_register(&Out_ronsecs_lsrc);
	if (ret) {
		dbgerr("log_register failed for Out_ronsecs_lsrc (%d)", ret);
		goto cleanup;
	}

	return (ret);

cleanup:
	log_deregister(&Out_ronsecs_lsrc);
	log_deregister(&Out_rcycles_lsrc);
	// shared between ronsecs and rcycles
	freeconst(Out_rcycles_lsrc.keys);
	freeconst(Out_rcycles_lsrc.metrics);
	return (ret);
}

/**
 * Deregister outputs from logging.
 * @return exec status
 */
static int outputs_log_deregister(void)
{
	int ret;

	ret = log_deregister(&Out_ronsecs_lsrc);
	if (ret) {
		dbgerr("log_deregister failed for Out_ronsecs_lsrc (%d)", ret);
	}
	ret = log_deregister(&Out_rcycles_lsrc);
	if (ret) {
		dbgerr("log_deregister failed for Out_rcycles_lsrc (%d)", ret);
	}
	// shared between ronsecs and rcycles
	freeconst(Out_rcycles_lsrc.keys);
	freeconst(Out_rcycles_lsrc.metrics);

	return (ret);
}

/**
 * Init outputs system.
 * This function clears internal  state.
 */
int outputs_init(void)
{
	memset(&Outputs, 0x00, sizeof(Outputs));

	return (ALL_OK);
}

/**
 * Online outputs.
 * Registers log.
 * @return exec status
 */
int outputs_online(void)
{
	int ret;

	ret = outputs_log_register();
	if (ret) {
		dbgerr("outputs_log_register failed (%d)", ret);
	}
	return (ALL_OK);
}

/**
 * Find an output by name.
 * @param t the type of output to look for
 * @param name the unique name to look for
 * @return the output id or error status
 */
int outputs_fbn(const enum e_output_type t, const char * name)
{
	outid_t id;
	int ret = -ENOTFOUND;

	if (!name)
		return (-EINVALID);

	switch (t) {
		case OUTPUT_RELAY:
			for (id = 0; id < Outputs.relays.last; id++) {
				if (!strcmp(Outputs.relays.all[id].name, name)) {
					ret = (int)outputs_id_to_outid(id);
					break;
				}
			}
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (ret);
}

/**
 * Return an output name.
 * @param t the type of output to look for
 * @param outid the output id
 * @return name or NULL on error
 */
const char * outputs_name(const enum e_output_type t, const outid_t outid)
{
	const outid_t id = outputs_outid_to_id(outid);
	const char * name = NULL;

	switch (t) {
		case OUTPUT_RELAY:
			if (likely(id < Outputs.relays.last))
				name = Outputs.relays.all[id].name;
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (name);
}

/**
 * Grab an output for exclusive use.
 * This function must be called by every active user (i.e. a state-setting user) of a relay to ensure exclusive write-access to the underlying relay.
 * @param t type of output
 * @param outid the output id to grab
 * @return exec status
 * @note This function should obviously be used only once, typically in online() call
 */
int outputs_grab(const enum e_output_type t, const outid_t outid)
{
	const outid_t id = outputs_outid_to_id(outid);
	int ret = -EINVALID;

	switch (t) {
		case OUTPUT_RELAY:
			if (likely(id < Outputs.relays.last))
				ret = relay_grab(&Outputs.relays.all[id]);
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (ret);
}

/**
 * Thaw an output relay that was previously grabbed.
 * @param t type of output
 * @param outid the output id to thaw
 * @return exec status
 */
int outputs_thaw(const enum e_output_type t, const outid_t outid)
{
	const outid_t id = outputs_outid_to_id(outid);
	int ret = -EINVALID;

	switch (t) {
		case OUTPUT_RELAY:
			if (likely(id < Outputs.relays.last))
				ret = relay_thaw(&Outputs.relays.all[id]);
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (ret);
}

/**
 * Get an output value.
 * @param t type of output
 * @param outid the output id to act on
 * @param value the requested state for the output
 * @return exec status
 */
int outputs_state_set(const enum e_output_type t, const outid_t outid, const int value)
{
	const outid_t id = outputs_outid_to_id(outid);
	int ret = -EINVALID;

	switch (t) {
		case OUTPUT_RELAY:
			if (likely(id < Outputs.relays.last))
				ret = relay_state_set(&Outputs.relays.all[id], (bool)value);
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (ret);
}

/**
 * Get an output value.
 * @param t type of output
 * @param outid the output id to read from
 * @return state or error
 */
int outputs_state_get(const enum e_output_type t, const outid_t rid)
{
	const outid_t id = outputs_outid_to_id(rid);
	int ret = -EINVALID;

	switch (t) {
		case OUTPUT_RELAY:
			if (likely(id < Outputs.relays.last))
				ret = relay_state_get(&Outputs.relays.all[id]);
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	return (ret);
}

/**
 * Offline outputs.
 * Deregister log.
 * @return exec status
 */
int outputs_offline(void)
{
	outputs_log_deregister();
	return (ALL_OK);
}

/**
 * Cleanup outputs system
 */
void outputs_exit(void)
{
	outid_t id;

	// clean all registered relays
	for (id = 0; id < Outputs.relays.last; id++)
		relay_clear(&Outputs.relays.all[id]);

	freeconst(Outputs.relays.all);
	memset(&Outputs, 0x00, sizeof(Outputs));
}
