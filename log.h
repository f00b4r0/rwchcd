//
//  log.h
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Log system API.
 */

#ifndef log_h
#define log_h

#include "rwchcd.h"

typedef uint32_t	log_version_t;	///< storage version type
typedef const char *	log_key_t;	///< storage keys type
typedef int32_t		log_value_t;	///< storage values type

/** backend unique identifiers */
enum e_log_bend {
	LBEND_FILE,
	LBEND_RRD,
};

/** Log data structure */
struct s_log_data {
	const log_key_t * restrict const keys;		///< the keys to log
	const log_value_t * restrict const values;	///< the values to log (1 per key)
	const unsigned int nkeys;			///< the number of keys
	const unsigned int nvalues;			///< the number of values (must be <= nkeys)
	const int interval;				///< a positive fixed interval between log requests or negative for random log events
};

/** Logging backend callbacks */
struct s_log_callbacks {
	/** backend unique identifier */
	enum e_log_bend backend;
	/** backend log create callback */
	int (*log_create)(const char * restrict const identifier, const struct s_log_data * const log_data);
	/** backend log update callback */
	int (*log_update)(const char * restrict const identifier, const struct s_log_data * const log_data);
};

int log_config(void);
int log_dump(const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data);

#endif /* log_h */
