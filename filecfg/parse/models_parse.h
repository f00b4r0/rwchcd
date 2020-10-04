//
//  filecfg/parse/models_parse.h
//  rwchcd
//
//  (C) 2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Models subsystem file configuration parsing API.
 */

#ifndef models_parse_h
#define models_parse_h

#include "filecfg_parser.h"

int filecfg_models_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node);

#endif /* models_parse_h */
