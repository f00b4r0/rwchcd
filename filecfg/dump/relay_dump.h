//
//  filecfg/dump/relay_dump.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Relay file configuration dumping API.
 */

#ifndef relay_dump_h
#define relay_dump_h

#include "relay.h"

void filecfg_relay_dump(const struct s_relay * r);

#endif /* relay_dump_h */
