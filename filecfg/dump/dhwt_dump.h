//
//  filecfg/dump/dhwt_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * DHWT file configuration dumping API.
 */

#ifndef dhwt_dump_h
#define dhwt_dump_h

#include "plant/dhwt.h"

int filecfg_dhwt_params_dump(const struct s_dhwt_params * restrict const params);
int filecfg_dhwt_dump(const struct s_dhwt * restrict const dhwt);

#endif /* dhwt_dump_h */
