//
//  hw_backends/hw_lib.h
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Hardware-related functions library API.
 */

#ifndef hw_lib_h
#define hw_lib_h

#include "rwchcd.h"
#include "timekeep.h"
#include "filecfg/parse/filecfg_parser.h"

typedef	uint32_t	res_t;	///< resistance value
#define RES_OHMMULT	16	///< resistor value precision: 16 -> better than 0.1 ohm precision

float hw_lib_pt1000_res_to_celsius(const res_t res);
float hw_lib_ni1000_res_to_celsius(const res_t res);

#endif /* hw_lib_h */
