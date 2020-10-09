//
//  hw_backends/mqtt/filecfg.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT backend file configuration API.
 */

#ifndef mqtt_filecfg_h
#define mqtt_filecfg_h

#include "filecfg/parse/filecfg_parser.h"

int mqtt_filecfg_parse(const struct s_filecfg_parser_node * const node);

#endif /* mqtt_filecfg_h */
