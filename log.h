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

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t	log_version_t;	///< log version type
typedef const char *	log_key_t;	///< log keys type
typedef int32_t		log_value_t;	///< log values type

/** log metrics types */
enum e_log_metric {
	LOG_METRIC_GAUGE,
	LOG_METRIC_COUNTER,
};

/** backend unique identifiers */
enum e_log_bend {
	LOG_BKEND_FILE,
	LOG_BKEND_RRD,
	LOG_BKEND_STATSD,	///< StatsD backend
};

/** discrete logging schedules */
enum e_log_sched {
	LOG_SCHED_1mn,
	LOG_SCHED_5mn,
	LOG_SCHED_15mn,
};

/** discrete logging intervals (seconds). Must match #e_log_sched */
enum e_log_intvl {
	LOG_INTVL_1mn = 60,
	LOG_INTVL_5mn = 300,
	LOG_INTVL_15mn = 900,
};

/** Log data structure */
struct s_log_data {
	const log_key_t * restrict keys;	///< pointer to array of keys to log
	const enum e_log_metric * restrict metrics;	///< pointer to array of metric types of each key (must have same number of elements as #keys)
	const log_value_t * restrict values;	///< pointer to array of values to log (1 per key)
	unsigned int nkeys;			///< the number of keys/metrics
	unsigned int nvalues;			///< the number of values (must be <= nkeys)
	int interval;				///< a positive fixed interval between log requests or negative for random log events
};

/** log data callback processor type */
typedef int (*log_data_cb_t)(struct s_log_data * const ldata, const void * const object);

/** Log source description. @warning strlen(basename) + strlen(identifier) must be < 240 */
struct s_log_source {
	enum e_log_sched log_sched;		///< log schedule, constrained by discrete e_log_sched values, mandatory
	const char * restrict basename;		///< log basename (e.g. "valve_" or "hcircuit_" to restrict identifier namespace. @note Used as part of filename. mandatory
	const char * restrict identifier;	///< log identifier within the basename namespace, must be unique in that namespace. @note Used as part of filename. mandatory
	log_version_t version;			///< log format version, mandatory
	log_data_cb_t logdata_cb;		///< callback to process the opaque object into an s_log_data structure ready for logging, mandatory
	const void * object;			///< opaque object to be handled by the logdata_cb callback, optional
};

/** Logging backend callbacks */
struct s_log_bendcbs {
	/** backend unique identifier */
	enum e_log_bend bkid;
	bool unversioned;				///< if true, the code will not try to track changes in log format version (speed gain)
	char separator;				///< single character separator used in concatenation of basename, identifier, etc
	/** optional backend log online callback */
	int (*log_online)(void);
	/** optional backend log offline callback */
	void (*log_offline)(void);
	/** backend log create callback */
	int (*log_create)(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data);
	/** backend log update callback */
	int (*log_update)(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data);
};

struct s_log {
	struct {
		bool configured;			///< true if properly configured (backends are online)
		struct s_log_bendcbs sync_bkend;	///< logging backend for synchronous (periodic) logs. Config expects a user string for backend name.
		struct s_log_bendcbs async_bkend;	///< logging backend for asynchronous logs. Config expects a user string for backend name.
	} set;
};

typedef void (*log_bkend_hook_t)(struct s_log_bendcbs * restrict const callbacks);

int log_async_dump(const char * restrict const identifier, const log_version_t * restrict const version, const struct s_log_data * restrict const log_data);
int log_register(const struct s_log_source * restrict const lsource);
int log_deregister(const struct s_log_source * restrict const lsource);
int log_init(void);
void log_exit(void);

#endif /* log_h */
