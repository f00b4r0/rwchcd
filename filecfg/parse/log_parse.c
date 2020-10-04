//
//  filecfg/log_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log subsystem file configuration parsing.
 */

#include <string.h>	// strcmp

#include "log_parse.h"
#include "log.h"
#include "filecfg_parser.h"
#include "rwchcd.h"

#include "log_file.h"
#ifdef HAS_RRD
 #include "log_rrd.h"
#endif
#include "log_statsd.h"

extern struct s_log Log;

static struct {
	const char * bkname;		///< backend identifier string (must be unique in system)
	log_bkend_hook_t hook;		///< backend hook routine
	parser_t parse;			///< backend config parser
	void (*dump)(void);		///< backend config dumper. @note if #parse is provided then #dump must be provided too.
	bool configured;		///< true if backend has been configured
} Log_known_bkends[] = {
	{ LOG_BKEND_FILE_NAME,		log_file_hook,		NULL, 				NULL,				false,	},	// file has no configuration
#ifdef HAS_RRD
	{ LOG_BKEND_RRD_NAME,		log_rrd_hook,		NULL,				NULL,				false,	},	// rrd has no configuration
#endif
	{ LOG_BKEND_STATSD_NAME,	log_statsd_hook,	log_statsd_filecfg_parse,	log_statsd_filecfg_dump,	false,	},
};


/*
 logging {
	 config {
		 enabled true;
		 sync_bkend "statsd";
		 async_bkend "file";
	 };
	 backends_conf {
		 backend "statsd" {
			 port "8000";
			 host "localhost";
		 };
	 };
 };
 */

static int log_config_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "enabled", true, NULL, NULL, },		// 0
		{ NODESTR, "sync_bkend", true, NULL, NULL, },		// 1
		{ NODESTR, "async_bkend", true, NULL, NULL, },		// 2
	};
	const struct s_filecfg_parser_node * currnode;
	struct s_log_bendcbs * restrict lbkend = NULL;
	unsigned int i, j;
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	for (j = 0; j < ARRAY_SIZE(parsers); j++) {
		currnode = parsers[j].node;
		switch (j) {
			case 0:
				Log.set.enabled = currnode->value.boolval;
				continue;	// skip the rest of the loop
				break;
			case 1:
				lbkend = &Log.set.sync_bkend;
				break;
			case 2:
				lbkend = &Log.set.async_bkend;
				break;
			default:
				break;	// can ever happen
		}

		for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
			if (!strcmp(Log_known_bkends[i].bkname, currnode->value.stringval)) {
				Log_known_bkends[i].hook(lbkend);
				break;
			}
		}

		if (ARRAY_SIZE(Log_known_bkends) == i) {
			ret = -EUNKNOWN;
			goto invaliddata;
		}
	}

	return (ALL_OK);

invaliddata:
	filecfg_parser_report_invaliddata(currnode);
	return (ret);
}

static int log_backend_conf_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
		if (!strcmp(Log_known_bkends[i].bkname, node->value.stringval)) {
			ret = Log_known_bkends[i].parse(priv, node);
			if (ALL_OK == ret)
				Log_known_bkends[i].configured = true;
			return (ret);
		}
	}

	return (-EUNKNOWN);
}

static int log_backends_conf_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "backend", log_backend_conf_parse));
}

/**
 * Parse logging subsystem configuration.
 * The parser expects a mandatory "config" node defining (by name) the sync and async backends to use (see #Log.set).
 * An optional "backends_conf" node can be provided, itself containing named "backend" subnodes detailing
 * the configuration parameters of backends requiring extra configuration.
 * @param priv unused
 * @param node a `logging` node
 * @return exec status
 */
int filecfg_log_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODELST,	"config",		true,	log_config_parse,		NULL, },
		{ NODELST,	"backends_conf",	false,	log_backends_conf_parse,	NULL, },
	};
	int ret;

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	return (filecfg_parser_run_parsers(priv, parsers, ARRAY_SIZE(parsers)));
}

// XXX currently must live in this file
#include "filecfg_dump.h"
void log_config_dump(void);
/**
* Dump the logging subsystem to config file.
* @return exec status
*/
int filecfg_log_dump(void)
{
	unsigned int i;

	filecfg_iprintf("logging {\n");
	filecfg_ilevel_inc();

	if (!Log.set.configured)
		goto empty;

	filecfg_iprintf("config {\n");
	filecfg_ilevel_inc();
	log_config_dump();
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");


	// check if we have a configured backend
	for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
		if (Log_known_bkends[i].configured)
			break;
	}

	if (i < ARRAY_SIZE(Log_known_bkends)) {
		filecfg_iprintf("backends_conf {\n");
		filecfg_ilevel_inc();

		for (i = 0; i < ARRAY_SIZE(Log_known_bkends); i++) {
			if (Log_known_bkends[i].configured) {
				filecfg_ilevel_inc();
				filecfg_iprintf("backend \"%s\" {\n", Log_known_bkends[i].bkname);
				Log_known_bkends[i].dump();
				filecfg_iprintf("};\n");
				filecfg_ilevel_dec();
			}
		}

		filecfg_ilevel_dec();
		filecfg_iprintf("};\n");
	}

empty:
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}
