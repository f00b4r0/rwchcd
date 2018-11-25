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

/** discrete logging intervals (seconds) */
enum e_log_intvl {
	LOG_INTVL_1mn = 60,
	LOG_INTVL_15mn = 900,
	LOG_INTVL_1h = 3600,	///< XXX TODO fix log_rrd.c
};

/** Log data structure */
struct s_log_data {
	const log_key_t * restrict keys;	///< the keys to log
	const log_value_t * restrict values;	///< the values to log (1 per key)
	unsigned int nkeys;			///< the number of keys
	unsigned int nvalues;			///< the number of values (must be <= nkeys)
	int interval;				///< a positive fixed interval between log requests or negative for random log events
};

/** log data callback processor type */
typedef int (*log_data_cb_t)(struct s_log_data * const ldata, const void * const object);

/** Log source description. @warning strlen(basename) + strlen(identifier) must be < 240 */
struct s_log_source {
	enum e_log_intvl interval;		///< log interval, constrained by discrete e_log_intvl values, mandatory
	const char * restrict basename;		///< log basename (e.g. "valve_" or "hcircuit_" to restrict identifier namespace. @note Used as part of filename. mandatory
	const char * restrict identifier;	///< log identifier within the basename namespace, must be unique in that namespace. @note Used as part of filename. mandatory
	log_version_t version;			///< log format version, mandatory
	log_data_cb_t logdata_cb;		///< callback to process the opaque object into an s_log_data structure ready for logging, mandatory
	const void * object;			///< opaque object to be handled by the logdata_cb callback, optional
};

/** Logging backend callbacks */
struct s_log_bendcbs {
	/** backend unique identifier */
	enum e_log_bend backend;
	/** backend log create callback */
	int (*log_create)(const char * restrict const identifier, const struct s_log_data * const log_data);
	/** backend log update callback */
	int (*log_update)(const char * restrict const identifier, const struct s_log_data * const log_data);
};

int log_dump(const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data);
int log_register(const struct s_log_source * restrict const lsource);
int log_deregister(const struct s_log_source * restrict const lsource);
int log_init(void);
void log_exit(void);

#endif /* log_h */