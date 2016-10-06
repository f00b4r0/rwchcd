//
//  rwchcd_storage.c
//  rwchcd
//
//  Created by Thibaut Varene on 06/10/2016.
//  Copyright © 2016 Slashdirt. All rights reserved.
//

#include <unistd.h>	// chdir
#include <stdio.h>	// fopen...
#include <string.h>	// memcmp
#include "rwchcd_storage.h"

#define RWCHCD_STORAGE_MAGIC "rwchcd"
#define RWCHCD_STORAGE_VERSION 1UL
#define STORAGE_PATH	"/var/lib/rwchcd/"

static const char Storage_magic[] = RWCHCD_STORAGE_MAGIC;
static const uint32_t Storage_version = RWCHCD_STORAGE_VERSION;

/**
 * Generic storage backend write call.
 * @param identifier a unique string identifying the object to backup
 * @param version a caller-defined version number
 * @param size size of the object argument
 * @param object the opaque object to store
 * @todo XXX TODO ERROR HANDLING
 */
int storage_dump(const char * restrict const identifier, const uint_fast32_t * restrict const version, const size_t size, const void * restrict const object)
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
	
	dbgmsg("identifier: %s, v: %d, sz: %d, ptr: %p");
	
	// write our global magic first
	fwrite(Storage_magic, ARRAY_SIZE(Storage_magic), 1, file);
	
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
 * @param size size of the object argument
 * @param object the opaque object to restore
 * @todo XXX TODO ERROR HANDLING
 */
int storage_fetch(const char * restrict const identifier, uint_fast32_t * restrict const version, const size_t size, void * restrict const object)
{
	FILE * restrict file = NULL;
	char magic[ARRAY_SIZE(Storage_magic)];
	typeof(Storage_version) sversion = 0;
	
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
	fread(magic, ARRAY_SIZE(Storage_magic), 1, file);
	
	// compare with current global magic
	if (memcmp(magic, Storage_magic, ARRAY_SIZE(Storage_magic)))
		return (-ESTORE);
	
	// then global version
	fread(&sversion, sizeof(sversion), 1, file);
	
	// compare with current global version
	if (memcmp(&sversion, &Storage_version, sizeof(Storage_version)))
		return (-ESTORE);
	
	dbgmsg("identifier: %s, v: %d, sz: %d, ptr: %p");

	// then read the local version
	fread(version, sizeof(*version), 1, file);
	
	// then read the object
	fread(object, size, 1, file);
	
	// finally close the file
	fclose(file);
	
	return (ALL_OK);
}