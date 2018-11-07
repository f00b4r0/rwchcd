//
//  storage.h
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Persistent storage API.
 */

#ifndef rwchcd_storage_h
#define rwchcd_storage_h

#include "rwchcd.h"

typedef uint32_t	storage_version_t;	///< storage version type
typedef const char *	storage_key_t;		///< storage keys type
typedef int32_t		storage_value_t;	///< storage values type

/** Log data structure */
struct s_log_data {
	const storage_key_t * restrict const keys;	///< the keys to log
	const storage_value_t * restrict const values;	///< the values to log (1 per key)
	const unsigned int nkeys;			///< the number of keys
	const unsigned int nvalues;			///< the number of values (must be <= nkeys)
	const int interval;				///< a positive fixed interval between log requests or negative for random log events
};

int storage_dump(const char * restrict const identifier, const storage_version_t * restrict const version, const void * restrict const object, const size_t size);
int storage_fetch(const char * restrict const identifier, storage_version_t * restrict const version, void * restrict const object, const size_t size);
int storage_log(const char * restrict const identifier, const storage_version_t * restrict const version, const struct s_log_data * restrict const log_data);

#endif /* rwchcd_storage_h */
