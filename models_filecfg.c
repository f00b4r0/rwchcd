//
//  models_filecfg.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models subsystem file configuration.
 */

#include "models_filecfg.h"
#include "models.h"
#include "filecfg.h"
#include "filecfg_parser.h"

extern struct s_models Models;

static int filecfg_bmodel_dump(const struct s_bmodel * restrict const bmodel)
{
	if (!bmodel)
		return (-EINVALID);

	if (!bmodel->set.configured)
		return (-ENOTCONFIGURED);

	filecfg_iprintf("bmodel \"%s\" {\n", bmodel->name);
	filecfg_ilevel_inc();

	if (FCD_Exhaustive || bmodel->set.logging)
		filecfg_iprintf("logging %s;\n", filecfg_bool_str(bmodel->set.logging));
	filecfg_iprintf("tau %ld;\n", timekeep_tk_to_sec(bmodel->set.tau));						// mandatory
	filecfg_iprintf("tid_outdoor"); filecfg_tempid_dump(bmodel->set.tid_outdoor);		// mandatory

	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

int models_filecfg_dump(void)
{
	struct s_bmodel_l * restrict bmodelelmt;

	filecfg_iprintf("models {\n");
	filecfg_ilevel_inc();
	for (bmodelelmt = Models.bmodels; bmodelelmt; bmodelelmt = bmodelelmt->next) {
		if (!bmodelelmt->bmodel->set.configured)
			continue;
		filecfg_bmodel_dump(bmodelelmt->bmodel);
	}
	filecfg_ilevel_dec();
	filecfg_iprintf("};\n");

	return (ALL_OK);
}

FILECFG_PARSER_BOOL_PARSE_SET_FUNC(s_bmodel, logging)
FILECFG_PARSER_TIME_PARSE_SET_FUNC(s_bmodel, tau)
FILECFG_PARSER_TID_PARSE_SET_FUNC(s_bmodel, tid_outdoor)

static int bmodel_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODEBOL, "logging", false, fcp_bool_s_bmodel_logging, NULL, },
		{ NODEINT|NODEDUR, "tau", true, fcp_tk_s_bmodel_tau, NULL, },
		{ NODELST, "tid_outdoor", true, fcp_tid_s_bmodel_tid_outdoor, NULL, },
	};
	struct s_bmodel * bmodel;
	const char * bmdlname = node->value.stringval;
	int ret;

	// we receive a 'bmodel' node with a valid string attribute which is the bmodel name

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	bmodel = models_new_bmodel(bmdlname);
	if (!bmodel)
		return (-EOOM);

	ret = filecfg_parser_run_parsers(bmodel, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);

	bmodel->set.configured = true;

	dbgmsg(3, 1, "matched \"%s\"", bmdlname);

	return (ret);
}

int models_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	return (filecfg_parser_parse_namedsiblings(priv, node->children, "bmodel", bmodel_parse));
}
