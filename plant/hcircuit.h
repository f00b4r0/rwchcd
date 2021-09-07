//
//  plant/hcircuit.h
//  rwchcd
//
//  (C) 2017,2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit operation API.
 */

#ifndef hcircuit_h
#define hcircuit_h

#include "rwchcd.h"

struct s_hcircuit;

int hcircuit_online(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
int hcircuit_offline(struct s_hcircuit * const circuit);
int hcircuit_run(struct s_hcircuit * const circuit) __attribute__((warn_unused_result));
void hcircuit_cleanup(struct s_hcircuit * circuit);

int hcircuit_make_bilinear(struct s_hcircuit * const circuit, temp_t tout1, temp_t twater1, temp_t tout2, temp_t twater2, uint_least16_t nH100);

#endif /* circuit_h */
