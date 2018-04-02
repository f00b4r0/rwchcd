//
//  storage.c
//  rwchcd
//
//  (C) 2016,2018 Thibaut VARENE
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

#include <unistd.h>	// chdir/write/close/unlink
#include <stdio.h>	// rename/fopen...
#include <string.h>	// memcmp
#include <time.h>	// time
#include <errno.h>	// errno
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>	// open/posix_fallocate/fadvise
#include <stdlib.h>	// mkstemp

#include "storage.h"

#define RWCHCD_STORAGE_MAGIC "rwchcd"
#define RWCHCD_STORAGE_VERSION 1UL
#ifndef RWCHCD_STORAGE_PATH
 #define RWCHCD_STORAGE_PATH	"/var/lib/rwchcd/"
#endif
#define STORAGE_TMPLATE	"tmpXXXXXX"

static const char Storage_magic[] = RWCHCD_STORAGE_MAGIC;
static const storage_version_t Storage_version = RWCHCD_STORAGE_VERSION;

/**
 * Generic storage backend write call.
 * Uses basic CoW, see https://lwn.net/Articles/457667/
 * @param identifier a unique string identifying the object to backup
 * @param version a caller-defined version number
 * @param object the opaque object to store
 * @param size size of the object argument
 * @todo add CRC
 */
int storage_dump(const char * restrict const identifier, const storage_version_t * restrict const version, const void * restrict const object, const size_t size)
{
	const size_t hdr_size = sizeof(Storage_magic) + sizeof(Storage_version) + sizeof(*version);
	size_t count = size + hdr_size;
	int fd, dir_fd, ret = -ESTORE;
	char tmpfile[] = STORAGE_TMPLATE;

	if (!identifier || !version || !object)
		return (-EINVALID);

	dir_fd = open(RWCHCD_STORAGE_PATH, O_RDONLY);
	if (dir_fd < 0)
		return (-ESTORE);

	// make sure we're in target wd
	if (fchdir(dir_fd))
		return (-ESTORE);

	// create new tmp file
	fd = mkstemp(tmpfile);
	if (fd < 0) {
		dbgmsg("failed to create %s (%s)", tmpfile, identifier);
		return (-ESTORE);
	}

	if (posix_fallocate(fd, 0, count)) {
		dbgmsg("couldn't fallocate %s (%s)", tmpfile, identifier);
		unlink(tmpfile);
		return (-ESTORE);
	}

	// from here, all subsequent writes are guaranteed to not fail due to lack of space

	// write our global magic first
	count -= write(fd, Storage_magic, sizeof(Storage_magic));

	// then our global version
	count -= write(fd, &Storage_version, sizeof(Storage_version));

	// then write the caller's version
	count -= write(fd, version, sizeof(*version));

	// then the caller's object
	count -= write(fd, object, size);

	// check all writes were complete and sync data
	if (count || fdatasync(fd)) {
		dbgmsg("incomplete write or failed to sync: %s (%s)", tmpfile, identifier);
		unlink(tmpfile);
		goto out;
	}

	// finally close the file
	if (close(fd)) {
		unlink(tmpfile);
		goto out;
	}

	// atomically move the file in place
	if (rename(tmpfile, identifier)) {
		dbgmsg("failed to rename %s to %s", tmpfile, identifier);
		goto out;
	}

	dbgmsg("identifier: %s, tmp: %s, v: %d, sz: %zu", identifier, tmpfile, *version, size);

	ret = ALL_OK;

out:
	// fsync() the containing directory. Works with O_RDONLY
	fdatasync(dir_fd);
	close(dir_fd);

	return (ret);
}

/**
 * Generic storage backend read call.
 * @param identifier a unique string identifying the object to recall
 * @param version a caller-defined version number
 * @param object the opaque object to restore
 * @param size size of the object argument
 * @todo add CRC check
 */
int storage_fetch(const char * restrict const identifier, storage_version_t * restrict const version, void * restrict const object, const size_t size)
{
	size_t count = size;
	int fd;
	char magic[ARRAY_SIZE(Storage_magic)];
	storage_version_t sversion = 0;
	
	if (!identifier || !version || !object)
		return (-EINVALID);
	
	// make sure we're in target wd
	if (chdir(RWCHCD_STORAGE_PATH))
		return (-ESTORE);
	
	// open stream
	fd = open(identifier, O_RDONLY);
	if (fd < 0) {
		dbgmsg("failed to open %s for reading", identifier);
		return (-ESTORE);
	}

	// read our global magic first
	read(fd, magic, sizeof(Storage_magic));

	// compare with current global magic
	if (memcmp(magic, Storage_magic, sizeof(Storage_magic)))
		return (-ESTORE);
	
	// then global version
	read(fd, &sversion, sizeof(sversion));

	// compare with current global version
	if (memcmp(&sversion, &Storage_version, sizeof(Storage_version)))
		return (-ESTORE);
	
	// then read the local version
	read(fd, version, sizeof(*version));

	// then read the object
	count -= read(fd, object, size);

	// finally close the file
	close(fd);

	if (count)
		return (-ESTORE);

	dbgmsg("identifier: %s, v: %d, sz: %zu", identifier, *version, size);

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
	const char fmt[] = "%%%us - %%u - %%u - %%u\n";	// magic - sversion - lversion - npairs
	char headformat[ARRAY_SIZE(fmt)+1];
	bool fcreate = false;
	unsigned int i;
	
	if (!identifier || !version)
		return (-EINVALID);
	
	// make sure we're in target wd
	if (chdir(RWCHCD_STORAGE_PATH))
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
		if (fscanf(file, headformat, magic, &sversion, &lversion, &i) != 4)
			fcreate = true;
		
		// compare with current global magic
		if (memcmp(magic, Storage_magic, sizeof(Storage_magic)))
			fcreate = true;
		
		// compare with current global version
		if (sversion != Storage_version)
			fcreate = true;
		
		// compare with current local version
		if (lversion != *version)
			fcreate = true;
		
		// compare with current number of pairs
		if (i != npairs)
			fcreate = true;
		
		if (fcreate)
			fclose(file);
	}
	
	if (fcreate) {
		file = fopen(identifier, "w");	// create/truncate
		if (!file)
			return (-ESTORE);

		// write our header first
		fprintf(file, headformat, Storage_magic, Storage_version, *version, npairs);
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
