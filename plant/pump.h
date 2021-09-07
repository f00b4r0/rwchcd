//
//  plant/pump.h
//  rwchcd
//
//  (C) 2017 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Pump operation API.
 */

#ifndef pump_h
#define pump_h

#include <stdbool.h>

#define FORCE	true	///< to force pump state (bypass cooldown), see pump_set_state()
#define NOFORCE	false	///< to not force pump state (let cooldown operate), see pump_set_state()

struct s_pump;

void pump_cleanup(struct s_pump * restrict pump);
struct s_pump * pump_virtual_new(struct s_pump * restrict const pump);
int pump_grab(struct s_pump * restrict pump);
int pump_thaw(struct s_pump * restrict pump);
int pump_online(struct s_pump * restrict const pump) __attribute__((warn_unused_result));
int pump_set_state(struct s_pump * restrict const pump, bool req_on, bool force_state) __attribute__((warn_unused_result));
int pump_get_state(const struct s_pump * restrict const pump);
int pump_set_dhwt_use(struct s_pump * const pump, bool used);
int pump_get_dhwt_use(const struct s_pump * const pump);
int pump_shutdown(struct s_pump * restrict const pump);
int pump_offline(struct s_pump * restrict const pump);
int pump_run(struct s_pump * restrict const pump) __attribute__((warn_unused_result));

bool pump_is_online(const struct s_pump * const pump);
const char * pump_name(const struct s_pump * const pump);
bool pump_is_shared(const struct s_pump * const pump);

#endif /* pump_h */
