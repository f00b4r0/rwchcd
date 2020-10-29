//
//  storage.c
//  rwchcd
//
//  (C) 2016,2018-2020 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Persistent storage implementation.
 * Currently a quick hack based on Berkeley DB.
 * This implementation is very inefficient: among other issues, 
 * we keep open()ing/close()ing files every time. Open once + frequent flush
 * and close at program end would be better, but the fact is that this subsystem
 * probably shouldn't use flat files at all, hence the lack of effort to improve this.
 * @warning no check is performed for @b identifier collisions in any of the output functions.
 * @note the backend can handle arbitrarily long identifiers and object sizes, within the Berkeley DB limits.
 */

#include <stdlib.h>	// free
#include <unistd.h>	// chdir
#include <string.h>	// memset/strdup
#include <db.h>

#include "storage.h"
#include "rwchcd.h"

#define STORAGE_VERSION		1UL

#define STORAGE_DB		"rwchcd.db"	///< BDB filename
#define STORAGE_DB_VKEY		"version"	///< version key
#define STORAGE_DB_OKEY		"object"	///< object key

static const storage_version_t Storage_version = STORAGE_VERSION;
bool Storage_configured = false;
const char * Storage_path = NULL;

/**
 * Generic storage backend write call.
 * @param identifier a unique string identifying the object to backup
 * @param version a caller-defined version number
 * @param object the opaque object to store
 * @param size size of the object argument
 */
int storage_dump(const char * restrict const identifier, const storage_version_t * restrict const version, const void * restrict const object, const size_t size)
{
	DB *dbp;
	DBT key, data;
	int dbret, ret = -ESTORE;

	if (!Storage_configured)
		return (-ENOTCONFIGURED);

	if (!identifier || !version || !object)
		return (-EINVALID);

	dbret = db_create(&dbp, NULL, 0);
	if (dbret) {
		dbgerr("db_create: %s", db_strerror(dbret));
		goto failret;
	}

	dbret = dbp->open(dbp, NULL, STORAGE_DB, identifier, DB_BTREE, DB_CREATE, 0);
	if (dbret) {
		dbgerr("db->open \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	// store version
	key.data = STORAGE_DB_VKEY;
	key.size = sizeof(STORAGE_DB_VKEY);
	data.data = version;
	data.size = sizeof(*version);

	dbret = dbp->put(dbp, NULL, &key, &data, 0);
	if (dbret) {
		dbgerr("db->put version \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	// store object
	key.data = STORAGE_DB_OKEY;
	key.size = sizeof(STORAGE_DB_OKEY);
	data.data = object;
	data.size = size;

	dbret = dbp->put(dbp, NULL, &key, &data, 0);
	if (dbret) {
		dbgerr("db->put object \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	dbgmsg(1, 1, "identifier: \"%s\", v: %d, sz: %zu", identifier, *version, size);

	ret = ALL_OK;

faildb:
	dbp->close(dbp, 0);
failret:
	return (ret);
}

/**
 * Generic storage backend read call.
 * @param identifier a unique string identifying the object to recall
 * @param version a caller-defined version number
 * @param object the opaque object to restore
 * @param size size of the object argument
 */
int storage_fetch(const char * restrict const identifier, storage_version_t * restrict const version, void * restrict const object, const size_t size)
{
	DB *dbp;
	DBT key, data;
	int dbret, ret = -ESTORE;

	if (!Storage_configured)
		return (-ENOTCONFIGURED);

	if (!identifier || !version || !object)
		return (-EINVALID);
	
	dbret = db_create(&dbp, NULL, 0);
	if (dbret) {
		dbgerr("db_create: %s", db_strerror(dbret));
		goto failret;
	}

	dbret = dbp->open(dbp, NULL, STORAGE_DB, identifier, DB_BTREE, DB_RDONLY, 0);
	if (dbret) {
		dbgerr("db->open \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	// get version
	key.data = STORAGE_DB_VKEY;
	key.size = sizeof(STORAGE_DB_VKEY);
	data.data = version;
	data.ulen = sizeof(*version);
	data.flags = DB_DBT_USERMEM;

	dbret = dbp->get(dbp, NULL, &key, &data, 0);
	if (dbret) {
		dbgerr("db->get version \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	// get object
	key.data = STORAGE_DB_OKEY;
	key.size = sizeof(STORAGE_DB_OKEY);
	data.data = object;
	data.ulen = size;
	data.flags = DB_DBT_USERMEM;

	dbret = dbp->get(dbp, NULL, &key, &data, 0);
	if (dbret) {
		dbgerr("db->get object \"%s\": %s", identifier, db_strerror(dbret));
		goto faildb;
	}

	dbgmsg(1, 1, "identifier: \"%s\", v: %d, sz: %zu", identifier, *version, size);

	ret = ALL_OK;

faildb:
	dbp->close(dbp, 0);
failret:
	return (ret);
}

/** Quick hack. @warning no other chdir should be performed */
int storage_online(void)
{
	// if we don't have a configured path, fallback to default
	if (!Storage_path)
		Storage_path = strdup(RWCHCD_STORAGE_PATH);	// not the most efficient use of memory

	// make sure we're in target wd. XXX This updates wd for all threads
	if (chdir(Storage_path)) {
		perror(Storage_path);
		return (-ESTORE);
	}

	Storage_configured = true;

	return (ALL_OK);
}

bool storage_isconfigured(void)
{
	return (Storage_configured);
}

void storage_exit(void)
{
	Storage_configured = false;
	free((void *)Storage_path);
	Storage_path = NULL;
}
