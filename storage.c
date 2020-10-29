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
 * This implementation is inefficient: among other issues, we keep open()ing/close()ing DB every time.
 * @warning no check is performed for @b identifier collisions in any of the output functions.
 * @warning in various places where this code is used, no struct marshalling is performed: it is assumed that padding won't change as long as the underlying
 * structures are unmodified.
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
static DB_ENV *dbenvp;

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

	dbret = db_create(&dbp, dbenvp, 0);
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
	
	dbret = db_create(&dbp, dbenvp, 0);
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
	DB *dbp;
	DBT key, data;
	storage_version_t sv;
	int dbret;

	// if we don't have a configured path, fallback to default
	if (!Storage_path)
		Storage_path = strdup(RWCHCD_STORAGE_PATH);	// not the most efficient use of memory

	// make sure we're in target wd. XXX This updates wd for all threads
	if (chdir(Storage_path)) {
		perror(Storage_path);
		return (-ESTORE);
	}

	dbret = db_env_create(&dbenvp, 0);
	if (dbret) {
		dbgerr("db_env_create: %s", db_strerror(dbret));
		goto failret;
	}

	dbret = dbenvp->open(dbenvp, Storage_path, DB_INIT_CDB | DB_INIT_MPOOL | DB_CREATE | DB_PRIVATE | DB_THREAD, 0);
	if (dbret) {
		dbgerr("dbenvp->open: %s", db_strerror(dbret));
		goto failenv;
	}

	dbret = db_create(&dbp, dbenvp, 0);
	if (dbret) {
		dbgerr("db_create: %s", db_strerror(dbret));
		goto failenv;
	}

	dbret = dbp->open(dbp, NULL, STORAGE_DB, "storage_version", DB_BTREE, DB_CREATE, 0);
	if (dbret) {
		/// @todo handle DB_OLD_VERSION -> DB-upgrade()
		dbgerr("db->open: %s", db_strerror(dbret));
		goto faildb;
	}

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	// get version
	key.data = STORAGE_DB_VKEY;
	key.size = sizeof(STORAGE_DB_VKEY);
	data.data = &sv;
	data.ulen = sizeof(sv);
	data.flags = DB_DBT_USERMEM;

	dbret = dbp->get(dbp, NULL, &key, &data, 0);
	switch (dbret) {
		case 0:
			if (Storage_version == sv)
				goto end;
			// fallthrough
		case DB_NOTFOUND:
			dbgmsg(1, 1, "will create/truncate");
			break;	// we'll store the new value and trunc database
		default:
			dbgerr("db->get: %s", db_strerror(dbret));
			goto faildb;
	}

	// if we reach here, the database is outdated or nonexistent
	dbret = dbp->truncate(dbp, NULL, NULL, 0);
	if (dbret) {
		dbgerr("db->truncate: %s", db_strerror(dbret));
		goto faildb;
	}

	// store version
	key.data = STORAGE_DB_VKEY;
	key.size = sizeof(STORAGE_DB_VKEY);
	data.data = &Storage_version;
	data.size = sizeof(Storage_version);

	dbret = dbp->put(dbp, NULL, &key, &data, 0);
	if (dbret) {
		dbgerr("db->put version: %s", db_strerror(dbret));
		goto faildb;
	}

end:
	dbp->close(dbp, 0);
	Storage_configured = true;
	return (ALL_OK);

faildb:
	dbp->close(dbp, 0);
failenv:
	dbenvp->close(dbenvp, 0);
failret:
	return (-ESTORE);
}

bool storage_isconfigured(void)
{
	return (Storage_configured);
}

void storage_exit(void)
{
	if (!Storage_configured)
		return;

	Storage_configured = false;

	dbenvp->close(dbenvp, 0);

	free((void *)Storage_path);
	Storage_path = NULL;
}
