//
//  filecfg/dump/log_dump.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration dumping.
 */

#include "log_dump.h"
#include "filecfg_dump.h"
#include "log/log.h"

#include "log/log_file.h"
#ifdef HAS_RRD
 #include "log/log_rrd.h"
#endif
#include "log/log_statsd.h"
#ifdef HAS_MQTT
 #include "log/log_mqtt.h"
#endif

extern struct s_log Log;

static void log_config_dump_bkend(const struct s_log_bendcbs * restrict const lbkend)
{
	filecfg_iprintf("bkend ");

	switch (lbkend->bkid) {
		case LOG_BKEND_FILE:
			filecfg_printf("\"%s\";\n", LOG_BKEND_FILE_NAME);
			break;
#ifdef HAS_RRD
		case LOG_BKEND_RRD:
			filecfg_printf("\"%s\";\n", LOG_BKEND_RRD_NAME);
			break;
#endif
		case LOG_BKEND_STATSD:
			filecfg_printf("\"%s\" {\n", LOG_BKEND_STATSD_NAME);
			filecfg_ilevel_inc();
			log_statsd_filecfg_dump();
			filecfg_ilevel_dec();
			filecfg_iprintf("};\n");
			break;
#ifdef HAS_MQTT
		case LOG_BKEND_MQTT:
			filecfg_printf("\"%s\" {\n", LOG_BKEND_MQTT_NAME);
			filecfg_ilevel_inc();
			log_mqtt_filecfg_dump();
			filecfg_ilevel_dec();
			filecfg_iprintf("};\n");
			break;
#endif
		default:
			filecfg_printf("\"unknown\";\n");
	}
}

static void log_config_dump(void)
{
	filecfg_dump_nodebool("enabled", Log.set.enabled);
	log_config_dump_bkend(Log.bkend);
}

/**
* Dump the log subsystem to config file.
* @return exec status
*/
int filecfg_log_dump(void)
{
	filecfg_iprintf("log {\n");
	filecfg_ilevel_inc();

	if (!Log.set.configured)
		goto empty;

	log_config_dump();

empty:
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
