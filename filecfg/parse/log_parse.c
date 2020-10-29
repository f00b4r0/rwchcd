//
//  filecfg/parse/log_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration parsing.
\verbatim
  log {
	  enabled true;
	  bkend "statsd" {
		  port "8125";
		  host "localhost";
	  };
  };
\endverbatim
 */

#include <string.h>	// strcmp

#include "log_parse.h"
#include "log/log.h"
#include "filecfg_parser.h"
#include "rwchcd.h"

#include "log/log_file.h"
#ifdef HAS_RRD
 #include "log/log_rrd.h"
#endif
#include "log/log_statsd.h"
#ifdef HAS_MQTT
 #include "log/log_mqtt.h"
#endif

extern struct s_log Log;

static struct {
	const char * bkname;		///< backend identifier string (mandatory, must be unique in system)
	log_bkend_hook_t hook;		///< backend hook routine (mandatory)
	parser_t parse;			///< backend config parser (optional)
} Log_known_bkends[] = {
	{ LOG_BKEND_FILE_NAME,		log_file_hook,		NULL,				},	// file has no configuration
#ifdef HAS_RRD
	{ LOG_BKEND_RRD_NAME,		log_rrd_hook,		NULL,				},	// rrd has no configuration
#endif
	{ LOG_BKEND_STATSD_NAME,	log_statsd_hook,	log_statsd_filecfg_parse,	},
#ifdef HAS_MQTT
	{ LOG_BKEND_MQTT_NAME,		log_mqtt_hook,		log_mqtt_filecfg_parse,		},
#endif
};

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_log, enabled)

static int log_parse_bkend(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_log * restrict const log = priv;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
		if (!strcmp(Log_known_bkends[i].bkname, node->value.stringval)) {
			Log_known_bkends[i].hook(&log->bkend);
			ret = ALL_OK;

			if (Log_known_bkends[i].parse)
				ret = Log_known_bkends[i].parse(priv, node);

			return (ret);
		}
	}

	return (-EUNKNOWN);
}

/**
 * Parse logging subsystem configuration.
 * @param priv unused
 * @param node a `logging` node
 * @return exec status
 */
int filecfg_log_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"enabled",	true, fcp_bool_s_log_enabled,	NULL, },
		{ NODESTR|NODESTC,	"bkend",	true, log_parse_bkend,		NULL, },
	};
	int ret;

	if (NODELST != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = log_init();
	if (ALL_OK != ret) {
		pr_err(_("Failed to initialize log subsystem (%d)"), ret);
		return (ret);
	}

	ret = filecfg_parser_run_parsers(&Log, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	Log.set.configured = true;

	// depends on storage (config)
	ret = log_online();
	if (ALL_OK != ret) {
		pr_err(_("Failed to online log subsystem (%d)"), ret);
		goto cleanup;
	}

	ret = rwchcd_add_finishcb(log_offline, log_exit);
	if (ALL_OK != ret)
		goto cleanup;

	return (ret);

cleanup:
	log_offline();
	log_exit();
	return (ret);
}
