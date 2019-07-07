//
//  logic.c
//  rwchcd
//
//  (C) 2016-2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Logic functions implementation for smart operation.
 * Smarter functions making use of time should be here and act as pre-filter for plant xxx_run() ops.
 * @todo implement a flexible logic system that would take user-definable conditions and user-selectable actions to trigger custom actions (for more flexible plants)
 */

#include <time.h>
#include <assert.h>

#include "config.h"
#include "runtime.h"
#include "lib.h"
#include "logic.h"
#include "models.h"

#include "hcircuit.h"
#include "dhwt.h"
#include "heatsource.h"

#include "hardware.h"	// for hardware_sensor_clone_temp()


