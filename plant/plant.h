//
//  plant/plant.h
//  rwchcd
//
//  (C) 2016-2017,2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Plant basic operation API.
 */

#ifndef rwchcd_plant_h
#define rwchcd_plant_h

#include "rwchcd.h"

struct s_plant;

int plant_online(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
int plant_offline(struct s_plant * restrict const plant);
int plant_run(struct s_plant * restrict const plant)  __attribute__((warn_unused_result));
struct s_pump * plant_fbn_pump(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_valve * plant_fbn_valve(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_hcircuit * plant_fbn_hcircuit(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_dhwt * plant_fbn_dhwt(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_heatsource * plant_fbn_heatsource(const struct s_plant * restrict const plant, const char * restrict const name);
struct s_plant * plant_new(void);
void plant_del(struct s_plant * plant);

#endif /* rwchcd_plant_h */
