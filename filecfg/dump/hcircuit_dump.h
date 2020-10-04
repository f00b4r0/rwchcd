//
//  filecfg/dump/hcircuit_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Heating circuit file configuration dumping API.
 */

#ifndef hcircuit_dump_h
#define hcircuit_dump_h

#include "plant/hcircuit.h"

int filecfg_hcircuit_params_dump(const struct s_hcircuit_params * restrict const params);
int filecfg_hcircuit_dump(const struct s_hcircuit * restrict const circuit);

#endif /* hcircuit_dump_h */
