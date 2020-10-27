//
//  filecfg/parse/models_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models subsystem file configuration parsing.
 */

#include "models_parse.h"
#include "models.h"
#include "filecfg_parser.h"
#include "inputs_parse.h"

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_bmodel, logging)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_bmodel, limit_tsummer)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_bmodel, limit_tfrost)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_bmodel, tau)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_bmodel, tid_outdoor)

static int bmodel_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"logging",		false,	fcp_bool_s_bmodel_logging,		NULL, },
		{ NODEFLT|NODEINT,	"limit_tsummer",	false,	fcp_temp_s_bmodel_limit_tsummer,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tfrost",		false,	fcp_temp_s_bmodel_limit_tfrost,		NULL, },
		{ NODEINT|NODEDUR,	"tau",			true,	fcp_tk_s_bmodel_tau,			NULL, },
		{ NODESTR,		"tid_outdoor",		true,	fcp_inputs_temperature_s_bmodel_tid_outdoor,	NULL, },
	};
	struct s_bmodel * bmodel;
	const char * bmdlname = node->value.stringval;
	int ret;

	// we receive a 'bmodel' node with a valid string attribute which is the bmodel name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	/* init models - clears data used by config */
	ret = models_init();
	if (ret) {
		pr_err(_("Failed to initialize models (%d)"), ret);
		return (ret);
	}

	bmodel = models_new_bmodel(bmdlname);
	if (!bmodel)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(bmodel, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	bmodel->set.configured = true;

	// bring the models online
	// depends on storage && inputs available (config) [inputs available depends on hardware]
	ret = models_online();
	if (ALL_OK != ret) {
		pr_err(_("Failed to bring models online"));
		goto cleanup;
	}

	ret = rwchcd_add_finishcb(models_offline, models_exit);
	if (ALL_OK != ret)
		goto cleanup;

	return (ret);

cleanup:
	models_offline();	// depends on storage && log && io available [io available depends on hardware]
	models_exit();
	return (ret);
}

int filecfg_models_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "bmodel", bmodel_parse));
}
