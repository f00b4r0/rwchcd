//
//  hw_lib.h
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
#include "filecfg_parser.h"

float hw_lib_pt1000_ohm_to_celsius(const uint_fast16_t ohm);
float hw_lib_ni1000_ohm_to_celsius(const uint_fast16_t ohm);

#endif /* hw_lib_h */
