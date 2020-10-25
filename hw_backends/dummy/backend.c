//
//  hw_backends/dummy/backend.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Dummy backend implementation.
 */

#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "hw_backends/hw_backends.h"
#include "timekeep.h"
#include "backend.h"

/**
 * Initialize dummy backend.
 * @param priv private backend data
 * @return error state
 */
__attribute__((warn_unused_result)) static int dummy_init(void * priv)
{
	struct s_dummy_pdata * restrict const hw = priv;

	if (!hw)
		return (-EINVALID);

	pr_log(_("Dummy backend init"));
	hw->run.initialized = true;

	return (ALL_OK);
}

/**
 * Get dummy backend online.
 * @param priv private backend data
 * @return exec status
 */
static int dummy_online(void * priv)
{
	struct s_dummy_pdata * restrict const hw = priv;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.initialized)
		return (-EINIT);

	hw->run.online = true;

	return (ALL_OK);
}

/**
 * Offline dummy backend.
 * @param priv private backend data
 * @return exec status
 */
static int dummy_offline(void * priv)
{
	struct s_dummy_pdata * restrict const hw = priv;

	if (!hw)
		return (-EINVALID);

	if (!hw->run.online)
		return (-EOFFLINE);

	hw->run.online = false;

	return (ALL_OK);
}

/**
 * Dummy backend exit routine.
 * @param priv private backend data. Will be invalid after the call.
 */
static void dummy_exit(void * priv)
{
	struct s_dummy_pdata * restrict const hw = priv;
	uint_fast8_t id;

	if (!hw)
		return;

	if (hw->run.online) {
		dbgerr("backend is still online!");
		return;
	}

	if (!hw->run.initialized)
		return;

	hw->run.initialized = false;

	for (id = 0; (id < hw->in.temps.l); id++)
		free((void *)hw->in.temps.all[id].name);

	for (id = 0; (id < hw->out.rels.l); id++)
		free((void *)hw->out.rels.all[id].name);

	free(hw->in.temps.all);
	free(hw->out.rels.all);

	free(hw);
}

/**
 * Return output name.
 * @param priv private backend data
 * @param type the type of requested output
 * @param oid id of the target internal output
 * @return target output name or NULL if error
 */
static const char * dummy_output_name(void * const priv, const enum e_hw_output_type type, const outid_t oid)
{
	struct s_dummy_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_OUTPUT_RELAY:
			str = (oid >= hw->out.rels.l) ? NULL : hw->out.rels.all[oid].name;
			break;
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * Set internal output state
 * @param priv private backend data
 * @param type the type of requested output
 * @param oid id of the internal output to modify
 * @param state pointer to target state of the output
 * @return exec status
 */
static int dummy_output_state_set(void * const priv, const enum e_hw_output_type type, const outid_t oid, const u_hw_out_state_t * const state)
{
	struct s_dummy_pdata * restrict const hw = priv;
	union {
		struct s_dummy_relay * r;
	} u;

	assert(hw && state);

	switch (type) {
		case HW_OUTPUT_RELAY:
			if (unlikely((oid >= hw->out.rels.l)))
				return (-EINVALID);
			u.r = &hw->out.rels.all[oid];
			if (unlikely(!u.r->set.configured))
				return (-ENOTCONFIGURED);
			u.r->run.state = state->relay;
			dbgmsg(1, 1, "relay \"%s\" new state: %d", u.r->name, u.r->run.state);
			break;
		case HW_OUTPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Get internal output state.
 * @param priv private backend data
 * @param type the type of requested output
 * @param oid id of the internal output to modify
 * @param state pointer in which the current state of the output will be stored
 * @return exec status
 */
static int dummy_output_state_get(void * const priv, const enum e_hw_output_type type, const outid_t oid, u_hw_out_state_t * const state)
{
	struct s_dummy_pdata * restrict const hw = priv;
	union {
		struct s_dummy_relay * r;
	} u;

	assert(hw && state);

	switch (type) {
		case HW_OUTPUT_RELAY:
			if (unlikely((oid >= hw->out.rels.l)))
				return (-EINVALID);
			u.r = &hw->out.rels.all[oid];
			if (unlikely(!u.r->set.configured))
				return (-ENOTCONFIGURED);
			state->relay = u.r->run.state;
			break;
		case HW_OUTPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Return input name.
 * @param priv private backend data
 * @param type the type of requested input
 * @param inid id of the target internal input
 * @return target input name or NULL if error
 */
static const char * dummy_input_name(void * const priv, const enum e_hw_input_type type, const inid_t inid)
{
	struct s_dummy_pdata * restrict const hw = priv;
	const char * str;

	assert(hw);

	switch (type) {
		case HW_INPUT_TEMP:
			str = (inid >= hw->in.temps.l) ? NULL : hw->in.temps.all[inid].name;
			break;
		case HW_INPUT_SWITCH:
		case HW_OUTPUT_NONE:
		default:
			str = NULL;
			break;
	}

	return (str);
}

/**
 * Dummy get input value.
 * @param priv private backend data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param value location to copy the current value of the input
 * @return exec status
 */
int dummy_input_value_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, u_hw_in_value_t * const value)
{
	struct s_dummy_pdata * restrict const hw = priv;
	union {
		struct s_dummy_temperature * t;
	} u;

	assert(hw && value);

	switch (type) {
		case HW_INPUT_TEMP:
			if (unlikely((inid >= hw->in.temps.l)))
				return (-EINVALID);
			u.t = &hw->in.temps.all[inid];
			if (unlikely(!u.t->set.configured))
				return (-ENOTCONFIGURED);
			value->temperature = u.t->set.value;
			break;
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	return (ALL_OK);
}

/**
 * Dummy get input last update time.
 * @param priv private backend data
 * @param type the type of requested output
 * @param inid id of the internal output to modify
 * @param ctime location to copy the input update time.
 * @return exec status
 */
static int dummy_input_time_get(void * const priv, const enum e_hw_input_type type, const inid_t inid, timekeep_t * const ctime)
{
	struct s_dummy_pdata * restrict const hw = priv;

	assert(hw && ctime);

	switch (type) {
		case HW_INPUT_TEMP:
			if (unlikely((inid >= hw->in.temps.l)))
				return (-EINVALID);
			if (unlikely(!hw->in.temps.all[inid].set.configured))
				return (-ENOTCONFIGURED);
			break;
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			return (-EINVALID);
	}

	*ctime = timekeep_now();

	return (ALL_OK);
}

/**
 * Find input id by name.
 * @param priv private backend data
 * @param type the type of requested input
 * @param name target name to look for
 * @return error if not found or input id
 */
int dummy_input_ibn(void * const priv, const enum e_hw_input_type type, const char * const name)
{
	const struct s_dummy_pdata * restrict const hw = priv;
	inid_t id;
	int ret = -ENOTFOUND;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_INPUT_TEMP:
			for (id = 0; (id < hw->in.temps.l); id++) {
				if (!hw->in.temps.all[id].set.configured)
					continue;
				if (!strcmp(hw->in.temps.all[id].name, name)) {
					ret = (int)id;
					break;
				}
			}
			break;
		case HW_INPUT_SWITCH:
		case HW_INPUT_NONE:
		default:
			ret = -EINVALID;
			break;
	}

	return (ret);
}

/**
 * Find output id by name.
 * @param priv private backend data
 * @param type the type of requested output
 * @param name target name to look for
 * @return error if not found or output id
 */
int dummy_output_ibn(void * const priv, const enum e_hw_output_type type, const char * const name)
{
	const struct s_dummy_pdata * restrict const hw = priv;
	outid_t id;
	int ret = -ENOTFOUND;

	assert(hw);

	if (!name)
		return (-EINVALID);

	switch (type) {
		case HW_OUTPUT_RELAY:
			for (id = 0; (id < hw->out.rels.l); id++) {
				if (!hw->out.rels.all[id].set.configured)
					continue;
				if (!strcmp(hw->out.rels.all[id].name, name)) {
					ret = (int)id;
					break;
				}
			}
			break;
		case HW_OUTPUT_NONE:
		default:
			ret = -EINVALID;
			break;
	}

	return (ret);
}

/** Hardware callbacks for dummy backend */
static const struct s_hw_callbacks dummy_callbacks = {
	.init = dummy_init,
	.exit = dummy_exit,
	.online = dummy_online,
	.offline = dummy_offline,
	.input_value_get = dummy_input_value_get,
	.input_time_get = dummy_input_time_get,
	.output_state_get = dummy_output_state_get,
	.output_state_set = dummy_output_state_set,
	.input_ibn = dummy_input_ibn,
	.output_ibn = dummy_output_ibn,
	.input_name = dummy_input_name,
	.output_name = dummy_output_name,
};

/**
 * Backend register wrapper.
 * @param priv private backend data
 * @param name user-defined name
 * @return exec status
 */
int dummy_backend_register(void * priv, const char * const name)
{
	if (!priv)
		return (-EINVALID);

	return (hw_backends_register(&dummy_callbacks, priv, name));
}
