//
//  rwchcd_storage.c
//  rwchcd
//
//  (C) 2016 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Persistent storage implementation.
 * Currently a buggy quick hack based on files.
 * This implementation is very inefficient: among other issues, 
 * we keep open()ing/close()ing files every time. Open once + frequent flush
 * and close at program end would be better, but the fact is that this subsystem
 * probably shouldn't use flat files at all, hence the lack of effort to improve this.
 * Timed logs would be better of an RRD (librrd is sadly completely undocumented),
 * and generally speaking a database with several tables makes more sense.
 * @bug no check is performed for @b identifier collisions in any of the output functions.
 */

#include <unistd.h>	// chdir
#include <stdio.h>	// fopen...
#include <string.h>	// memcmp
#include <time.h>	// time
#include <errno.h>	// errno

#include "rwchcd_storage.h"

#define RWCHCD_STORAGE_MAGIC "rwchcd"
#define RWCHCD_STORAGE_VERSION 1UL
#define STORAGE_PATH	"/var/lib/rwchcd/"

static const char Storage_magic[] = RWCHCD_STORAGE_MAGIC;
static const storage_version_t Storage_version = RWCHCD_STORAGE_VERSION;

/**
 * Generic storage backend write call.
 * @param identifier a unique string identifying the object to backup
 * @param version a caller-defined version number
 * @param object the opaque object to store
 * @param size size of the object argument
 * @todo XXX TODO ERROR HANDLING
 * @todo add CRC
 */
int storage_dump(const char * restrict const identifier, const storage_version_t * restrict const version, const void * restrict const object, const size_t size)
{
	FILE * restrict file = NULL;
	
	if (!identifier || !version || !object)
		return (-EINVALID);
	
	// make sure we're in target wd
	if (chdir(STORAGE_PATH))
		return (-ESTORE);
	
	// open stream
	file = fopen(identifier, "w");
	if (!file)
		return (-ESTORE);
	
	dbgmsg("identifier: %s, v: %d, sz: %zu, ptr: %p",
	       identifier, *version, size, object);
	
	// write our global magic first
	fwrite(Storage_magic, sizeof(Storage_magic), 1, file);
	
	// then our global version
	fwrite(&Storage_version, sizeof(Storage_version), 1, file);
	
	// then write the caller's version
	fwrite(version, sizeof(*version), 1, file);
	
	// then the caller's object
	fwrite(object, size, 1, file);

	// finally close the file
	fclose(file);
	
	return (ALL_OK);
}

/**
 * Generic storage backend read call.
 * @param identifier a unique string identifying the object to recall
 * @param version a caller-defined version number
 * @param object the opaque object to restore
 * @param size size of the object argument
 * @todo XXX TODO ERROR HANDLING
 * @todo add CRC check
 */
int storage_fetch(const char * restrict const identifier, storage_version_t * restrict const version, void * restrict const object, const size_t size)
{
	FILE * restrict file = NULL;
	char magic[ARRAY_SIZE(Storage_magic)];
	storage_version_t sversion = 0;
	
	if (!identifier || !version || !object)
		return (-EINVALID);
	
	// make sure we're in target wd
	if (chdir(STORAGE_PATH))
		return (-ESTORE);
	
	// open stream
	file = fopen(identifier, "r");
	if (!file)
		return (-ESTORE);
	
	// read our global magic first
	fread(magic, sizeof(Storage_magic), 1, file);
	
	// compare with current global magic
	if (memcmp(magic, Storage_magic, sizeof(Storage_magic)))
		return (-ESTORE);
	
	// then global version
	fread(&sversion, sizeof(sversion), 1, file);
	
	// compare with current global version
	if (memcmp(&sversion, &Storage_version, sizeof(Storage_version)))
		return (-ESTORE);
	
	// then read the local version
	fread(version, sizeof(*version), 1, file);
	
	// then read the object
	fread(object, size, 1, file);
	
	dbgmsg("identifier: %s, v: %d, sz: %zu, ptr: %p",
	       identifier, *version, size, object);
	
	// finally close the file
	fclose(file);
	
	return (ALL_OK);
}

/**
 * Generic storage backend keys/values log call.
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 * @param keys the keys to log
 * @param values the values to log (1 per key)
 * @param npairs the number of key/value pairs
 * @todo XXX TODO ERROR HANDLING
 */
int storage_log(const char * restrict const identifier, const storage_version_t * restrict const version, storage_keys_t keys[], storage_values_t values[], unsigned int npairs)
{
	FILE * restrict file = NULL;
	char magic[ARRAY_SIZE(Storage_magic)];
	storage_version_t sversion = 0, lversion = 0;
	const char fmt[] = "%%%us - %%u - %%u\n";
	char headformat[ARRAY_SIZE(fmt)+1];
	bool fcreate = false;
	unsigned int i;
	
	if (!identifier || !version)
		return (-EINVALID);
	
	// make sure we're in target wd
	if (chdir(STORAGE_PATH))
		return (-ESTORE);
	
	// open stream
	file = fopen(identifier, "r+");
	if (!file) {
		if (ENOENT == errno)
			fcreate = true;
		else
			return (-ESTORE);
	}

	// build header format
	sprintf(headformat, fmt, ARRAY_SIZE(magic));
	
	if (!fcreate) {	// we have a file, does it work for us?
		// read top line first
		if (fscanf(file, headformat, magic, &sversion, &lversion) != 3)
			fcreate = true;
		
		// compare with current global magic
		if (memcmp(magic, Storage_magic, sizeof(Storage_magic)))
			fcreate = true;
		
		// compare with current global version
		if (memcmp(&sversion, &Storage_version, sizeof(Storage_version)))
			fcreate = true;
		
		// compare with current global version
		if (memcmp(&lversion, version, sizeof(*version)))
			fcreate = true;
		
		if (fcreate)
			fclose(file);
	}
	
	if (fcreate) {
		file = fopen(identifier, "w");	// create/truncate
		if (!file)
			return (-ESTORE);

		// write our header first
		fprintf(file, headformat, Storage_magic, Storage_version, *version);
		// write csv header
		fprintf(file, "time;");
		for (i = 0; i < npairs; i++)
			fprintf(file, "%s;", keys[i]);
		fprintf(file, "\n");
	}
	else {
		if(fseek(file, 0, SEEK_END))	// append
			return (-ESTORE);
	}

	// write csv data
	fprintf(file, "%ld;", time(NULL));
	for (i = 0; i < npairs; i++)
		fprintf(file, "%d;", values[i]);
	fprintf(file, "\n");

	// finally close the file
	fclose(file);
	
	return (ALL_OK);
}
