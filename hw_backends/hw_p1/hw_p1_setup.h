//
//  hw_backends/hw_p1/hw_p1_setup.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware Prototype 1 setup interface.
 */

#ifndef hw_p1_setup_h
#define hw_p1_setup_h

#include "rwchcd.h"
#include "hw_p1.h"
#include "hw_backends/hw_lib.h"

void * hw_p1_setup_new(void);
void hw_p1_setup_del(struct s_hw_p1_pdata * restrict const hw);

int hw_p1_setup_setbl(struct s_hw_p1_pdata * restrict const hw, const uint8_t percent);
int hw_p1_setup_setnsamples(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t nsamples);

int hw_p1_setup_relay_request(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_p1_relay * restrict const relay) __attribute__((warn_unused_result));
int hw_p1_setup_relay_release(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t id);
int hw_p1_setup_sensor_configure(struct s_hw_p1_pdata * restrict const hw, const struct s_hw_p1_sensor * restrict const sensor) __attribute__((warn_unused_result));
int hw_p1_setup_sensor_deconfigure(struct s_hw_p1_pdata * restrict const hw, const uint_fast8_t id);

#endif /* hw_p1_setup_h */
