//
//  filecfg/parse/storage_parse.c
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Storage subsystem file configuration parsing.
 *
\verbatim
 storage {
	 path "/var/lib/rwchcd";
 };
\endverbatim
 */

#include <string.h>	// strdup
#include <stdlib.h>	// free

#include "storage_parse.h"
#include "filecfg_parser.h"
#include "rwchcd.h"
#include "storage.h"

extern const char * Storage_path;

/**
 * Configure the storage subsystem.
 * @param node the `storage` node which contains a single `path` node, itself
 * a string pointing to the @b absolute storage location.
 * @return exec status.
 */
int filecfg_storage_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "path", true, NULL, NULL, },		// 0
	};
	const struct s_filecfg_parser_node * currnode;
	const char * path;
	int ret;

	// we receive a 'storage' node

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	currnode = parsers[0].node;
	path = currnode->value.stringval;

	if (strlen(path) < 1)
		goto invaliddata;

	if ('/' != *path) {
		filecfg_parser_pr_err(_("Line %d: path \"%s\" is not absolute"), node->lineno, path);
		goto invaliddata;
	}

	// all good
	if (!Storage_path)
		Storage_path = strdup(path);
	else	// should never happen
		return (-EEXISTS);

	ret = rwchcd_add_subsyscb("storage", storage_online, NULL, storage_exit);
	if (ALL_OK != ret)
		storage_exit();

	return (ret);

invaliddata:
	return (-EINVALID);
}
