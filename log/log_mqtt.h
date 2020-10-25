//
//  log/log_mqtt.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * MQTT log API.
 */

#ifndef log_mqtt_h
#define log_mqtt_h

#include "log.h"
#include "filecfg/parse/filecfg_parser.h"

#define LOG_BKEND_MQTT_NAME	"mqtt"

void log_mqtt_hook(const struct s_log_bendcbs ** restrict const callbacks);
void log_mqtt_filecfg_dump(void);
int log_mqtt_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* log_mqtt_h */
