//
//  plant/pump.c
//  rwchcd
//
//  (C) 2017,2020-2021 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump operation implementation.
 *
 * The pump implementation supports:
 * - Cooldown timeout (to prevent short runs)
 * - Shared pumps
 *
 * @note the implementation doesn't really care about thread safety on the assumption that
 * each pump is managed exclusively by a parent entity and thus no concurrent operation is
 * ever expected to happen to a given pump, with the exception of _get_state() which is thread-safe.
 */

#include <stdlib.h>	// calloc/free
#include <string.h>	// memset
#include <assert.h>

#include "pump.h"
#include "io/outputs.h"
#include "alarms.h"
#include "pump_priv.h"

/**
 * Cleanup a pump.
 * Frees all pump-local resources
 * @param pump the (parent) pump to delete
 */
void pump_cleanup(struct s_pump * restrict pump)
{
	struct s_pump * p, * pn;

	if (!pump)
		return;

	assert(!pump->virt.parent);

	p = pump->virt.child;
	while (p) {
		pn = p->virt.child;
		free(p);
		p = pn;
	}

	free((void *)pump->name);
	pump->name = NULL;
}

/**
 * Create a virtual shared pump.
 * Virtual pumps do not allocate extra memory besides their own structure. In particular, name is shared with parent.
 * @param pump the parent (non-virtual) pump
 */
struct s_pump * pump_virtual_new(struct s_pump * restrict const pump)
{
	struct s_pump * p;

	if (!pump || !pump->set.shared)
		return NULL;

	assert(!pump->virt.parent);

	p = calloc(1, sizeof((*p)));
	if (!p)
		return NULL;

	// for virtual pumps we really only care about .run and .virt
	p->virt.parent = pump;
	p->virt.child = pump->virt.child;
	pump->virt.child = p;

	dbgmsg(1, 1, "virtual pump (%p), parent: \"%s\" (%p), child (%p)", p, pump->name, pump, p->virt.child);

	return (p);
}

/**
 * Grab a pump for use.
 * @param pump the (parent) pump to claim
 * @return exec status
 * @note in the current implementation of shared pumps, we do not (need to) keep a count of users.
 */
int pump_grab(struct s_pump * restrict pump)
{
	if (!pump)
		return (-EINVALID);

	assert(!pump->virt.parent);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	if (pump->run.grabbed)
		return (-EEXISTS);

	pump->run.grabbed = true;
	return (ALL_OK);
}

/**
 * Put pump online.
 * Perform all necessary actions to prepare the pump for service
 * and mark it as online.
 * @param pump target (parent) pump
 * @return exec status
 */
int pump_online(struct s_pump * restrict const pump)
{
	int ret;

	if (!pump)
		return (-EINVALID);

	assert(!pump->virt.parent);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	ret = outputs_relay_grab(pump->set.rid_pump);
	if (ALL_OK != ret) {
		pr_err(_("\"%s\": Pump relay is unavailable (%d)"), pump->name, ret);
		return (-EMISCONFIGURED);
	}

	aser(&pump->run.online, true);

	return (ALL_OK);
}

/**
 * Set pump state.
 * @param pump target pump
 * @param req_on request pump on if true
 * @param force_state alters shared pump logic if true (used to ensure pump stops)
 * @return error code if any
 */
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state)
{
	const struct s_pump * p;

	if (unlikely(!pump))
		return (-EINVALID);

	// for virtual pump, online status is the parent's
	p = pump->virt.parent ? pump->virt.parent : pump;

	if (unlikely(!aler(&p->run.online)))
		return (-EOFFLINE);

	pump->run.req_on = req_on;
	pump->run.force_state = force_state;

	return (ALL_OK);
}

/**
 * Get pump state.
 * @param pump target pump
 * @return pump state
 * @note thread-safe by virtue of only calling outputs_relay_state_get()
 */
int pump_get_state(const struct s_pump * restrict const pump)
{
	const struct s_pump * p;

	if (unlikely(!pump))
		return (-EINVALID);

	// for virtual pump, query parent state
	p = pump->virt.parent ? pump->virt.parent : pump;

	if (unlikely(!aler(&p->run.online)))
		return (-EOFFLINE);

	// NOTE we could return remaining cooldown time if necessary
	return (outputs_relay_state_get(p->set.rid_pump));
}

/**
 * Shutdown an online pump.
 * Perform all necessary actions to completely shut down the pump.
 * @param pump target pump
 * @return exec status
 */
int pump_shutdown(struct s_pump * restrict const pump)
{
	if (unlikely(!pump))
		return (-EINVALID);

	return(pump_set_state(pump, OFF, NOFORCE));
}

/**
 * Put pump offline.
 * Perform all necessary actions to completely shut down the pump
 * and mark it as offline.
 * @param pump target (parent) pump
 * @return exec status
 */
int pump_offline(struct s_pump * restrict const pump)
{
	if (!pump)
		return (-EINVALID);

	assert(!pump->virt.parent);

	if (!pump->set.configured)
		return (-ENOTCONFIGURED);

	// unconditionally turn pump off
	(void)!outputs_relay_state_set(pump->set.rid_pump, false);

	outputs_relay_thaw(pump->set.rid_pump);

	memset(&pump->run, 0x00, sizeof(pump->run));
	//pump->run.online = false;	// handled by memset

	return(ALL_OK);
}

#define pump_failsafe(_pump)	(void)pump_shutdown(_pump)

/**
 * Run pump.
 * @param pump target (parent) pump
 * @return exec status
 * @note this function ensures that in the event of an error, the pump is put in a
 * failsafe state as defined in pump_failsafe().
 * @note Logic of shared pumps is as follows:
 *  - if *any* of the master or virtual pumps requests ON, the physical pump is ON;
 *  - *EXCEPT* if *any* of the master or virtual pumps has a FORCE OFF request.
 */
int pump_run(struct s_pump * restrict const pump)
{
	const struct s_pump * p;
	bool state, req, force;
	int ret;

	if (unlikely(!pump))
		return (-EINVALID);

	// we should only operate on plant's pump list
	assert(!pump->virt.parent);

	if (unlikely(!aler(&pump->run.online)))	// implies set.configured == true
		return (-EOFFLINE);

	state = !!outputs_relay_state_get(pump->set.rid_pump);	// assumed cannot fail
	req = pump->run.req_on;
	force = pump->run.force_state;

	if (force && !req)
		goto skipvirtual;

	if (pump->set.shared) {
		dbgmsg(2, 1, "\"%s\": parent (%p), req: %d, force: %d", pump->name, pump, req, force);
		for (p = pump->virt.child; p; p = p->virt.child) {
			dbgmsg(2, 1, "\"%s\": child (%p), req: %d, force: %d", pump->name, p, p->run.req_on, p->run.force_state);
			req |= p->run.req_on;
			force |= p->run.force_state;
			if (p->run.force_state && !p->run.req_on) {
				req = false;
				break;
			}
		}
	}

skipvirtual:
	dbgmsg(1, 1, "\"%s\": shared: %d, state: %d, req: %d, force: %d", pump->name, pump->set.shared, state, req, force);

	if (state == req)
		return (ALL_OK);

	ret = outputs_relay_state_set(pump->set.rid_pump, req);
	if (unlikely(ret < 0))
		goto fail;

	aser(&pump->run.state, req);

	return (ALL_OK);

fail:
	alarms_raise(ret, _("Pump \"%s\": failed to operate!"), pump->name);
	pump_failsafe(pump);
	return (ret);
}

/**
 * Test if pump is shared.
 * @param pump target (parent) pump
 * @return true if shared, false otherwise
 */
bool pump_is_shared(const struct s_pump * const pump)
{
	assert(pump);
	assert(!pump->virt.parent);
	return (pump->set.shared);
}

/**
 * Test if pump is online.
 * @param pump target pump
 * @return true if online, false otherwise
 */
bool pump_is_online(const struct s_pump * const pump)
{
	const struct s_pump * p;

	assert(pump);

	// for virtual pump, query parent state
	p = pump->virt.parent ? pump->virt.parent : pump;
	return (aler(&p->run.online));
}

/**
 * Get pump name.
 * @param pump target pump
 * @return pump name
 */
const char * pump_name(const struct s_pump * const pump)
{
	const struct s_pump * p;

	assert(pump);

	// for virtual pump, query parent name
	p = pump->virt.parent ? pump->virt.parent : pump;
	return (p->name);
}
