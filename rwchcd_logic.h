//
//  rwchcd_logic.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Logic functions API.
 */

#ifndef rwchcd_logic_h
#define rwchcd_logic_h

#include "rwchcd.h"
#include "rwchcd_plant.h"

int logic_circuit(struct s_heating_circuit * restrict const circuit);
int logic_dhwt(struct s_dhw_tank * restrict const dhwt);
int logic_heatsource(struct s_heatsource * restrict const heat);

#endif /* rwchcd_logic_h */
