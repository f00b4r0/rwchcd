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
 *
\verbatim
 models {
	 bmodel "name" {
		 log yes;
		 limit_tsummer 18.0;
		 limit_tfrost 3.0;
		 tau 20h;
		 tid_outdoor "outdoor";
	 };
 };
\endverbatim
 */

#include <stdlib.h>
#include <string.h>

#include "models_parse.h"
#include "models.h"
#include "filecfg_parser.h"
#include "inputs_parse.h"

extern struct s_models Models;

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_bmodel, log)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_bmodel, limit_tsummer)
FILECFG_PARSER_CELSIUS_PARSE_SET_FUNC(false, false, s_bmodel, limit_tfrost)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_bmodel, tau)
FILECFG_INPUTS_PARSER_TEMPERATURE_PARSE_SET_FUNC(s_bmodel, tid_outdoor)

static int bmodel_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL,		"log",			false,	fcp_bool_s_bmodel_log,			NULL, },
		{ NODEFLT|NODEINT,	"limit_tsummer",	true,	fcp_temp_s_bmodel_limit_tsummer,	NULL, },
		{ NODEFLT|NODEINT,	"limit_tfrost",		true,	fcp_temp_s_bmodel_limit_tfrost,		NULL, },
		{ NODEINT|NODEDUR,	"tau",			true,	fcp_tk_s_bmodel_tau,			NULL, },
		{ NODESTR,		"tid_outdoor",		true,	fcp_inputs_temperature_s_bmodel_tid_outdoor,	NULL, },
	};
	struct s_bmodel * restrict const bmodel = priv;
	int ret;

	// we receive a 'bmodel' node with a valid string attribute which is the bmodel name
	if (NODESTC != node->type)
		return (-EINVALID);

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	ret = filecfg_parser_run_parsers(bmodel, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	bmodel->name = strdup(node->value.stringval);
	if (!bmodel->name)
		return (-EOOM);

	bmodel->set.configured = true;

	return (ret);
}

static int models_bmodel_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_models * restrict const models = priv;
	struct s_bmodel * bmodel;
	int ret;

	if (models->bmodels.last >= models->bmodels.n)
		return (-EOOM);

	if (models_fbn_bmodel(node->value.stringval))
		return (-EEXISTS);

	bmodel = &models->bmodels.all[models->bmodels.last];
	ret = bmodel_parse(bmodel, node);
	if (ALL_OK == ret)
		models->bmodels.last++;

	return (ret);
}

static int models_bmodels_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_models * restrict const models = priv;
	unsigned int n;
	int ret;

	n = filecfg_parser_count_siblings(node->children, "bmodel");

	if (!n)
		return (-EEMPTY);

	if (n >= MODID_MAX)
		return (-ETOOBIG);

	models->bmodels.all = calloc(n, sizeof(models->bmodels.all[0]));
	if (!Models.bmodels.all)
		return (-EOOM);

	models->bmodels.n = (modid_t)n;
	models->bmodels.last = 0;

	ret = filecfg_parser_parse_namedsiblings(models, node->children, "bmodel", models_bmodel_parse);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);

cleanup:
	// todo: cleanup all bmodels (names)
	free(models->bmodels.all);
	return (ret);
}

int filecfg_models_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	int ret;

	/* init models - clears data used by config */
	ret = models_init();
	if (ret) {
		pr_err(_("Failed to initialize models (%d)"), ret);
		return (ret);
	}

	ret = models_bmodels_parse(&Models, node);
	if (ALL_OK != ret)
		return (ret);

	// bring the models online
	// depends on storage && log && inputs available (config) [inputs available depends on hardware]

	ret = rwchcd_add_subsyscb("models", models_online, models_offline, models_exit);
	if (ALL_OK != ret)
		goto cleanup;

	return (ALL_OK);
	
cleanup:
	models_exit();
	return (ret);
}
