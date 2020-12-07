//
//  hw_backends/hw_lib.h
//  rwchcd
//
//  (C) 2019-2020 Thibaut VARENE
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

/**
 * Known types of RTDs, identified by their element and temperature coefficient (ppm/K).
 * See #hw_lib_rtdtype_str for configuration strings.
 */
enum e_hw_lib_rtdt {
	HW_RTD_NONE = 0,	///< invalid - not configured
	HW_RTD_PT3750,
	HW_RTD_PT3770,
	HW_RTD_PT3850,
	HW_RTD_PT3902,
	HW_RTD_PT3911,
	HW_RTD_PT3916,
	HW_RTD_PT3920,
	HW_RTD_PT3928,
	HW_RTD_NI5000,
	HW_RTD_NI6180,
};

float hw_lib_rtd_res_to_celsius(const enum e_hw_lib_rtdt rtdtype, const res_t R0res, const res_t Rtres);

const char * hw_lib_print_rtdtype(const enum e_hw_lib_rtdt type);
int hw_lib_match_rtdtype(const char * str);

/**
 * Convert ohms to res_t format.
 * @param ohms value to convert.
 * @return the value correctly formatted.
 * @warning seconds must be < UINT32_MAX/RES_OHMMULT
 */
#define hw_lib_ohm_to_res(ohms)	(res_t)(ohms * RES_OHMMULT)

float hw_lib_res_to_ohm(const res_t res);

#endif /* hw_lib_h */
