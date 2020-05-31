//
//  storage.c
//  rwchcd
//
//  (C) 2016,2018-2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * Persistent storage implementation.
 * Currently an ugly quick hack based on files.
 * This implementation is very inefficient: among other issues, 
 * we keep open()ing/close()ing files every time. Open once + frequent flush
 * and close at program end would be better, but the fact is that this subsystem
 * probably shouldn't use flat files at all, hence the lack of effort to improve this.
 * Generally speaking a database with several tables makes more sense.
 * @warning no check is performed for @b identifier collisions in any of the output functions.
 * @todo consider using hashes instead of plaintext for filenames to remove the limitation on e.g. entity names.
 */

#include <unistd.h>	// chdir/write/close/unlink
#include <stdio.h>	// rename/fopen/perror...
#include <string.h>	// memcmp
#include <errno.h>	// errno
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>	// open/posix_fallocate/fadvise
#include <stdlib.h>	// mkstemp/malloc

#include "storage.h"
#include "rwchcd.h"

#define STORAGE_MAGIC		"rwchcd"
#define STORAGE_VERSION		1UL
#define STORAGE_TMPLATE		"tmpXXXXXX"

static const char Storage_magic[] = STORAGE_MAGIC;
static const storage_version_t Storage_version = STORAGE_VERSION;
bool Storage_configured = false;
const char * Storage_path = NULL;

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
	ssize_t count = (ssize_t)(size + hdr_size);
	int fd, dir_fd, ret = -ESTORE;
	char tmpfile[] = STORAGE_TMPLATE;

	//assert((size + hdr_size) < SSIZE_MAX);

	if (!Storage_configured)
		return (-ENOTCONFIGURED);

	if (!identifier || !version || !object)
		return (-EINVALID);

	dir_fd = open(Storage_path, O_RDONLY);
	if (dir_fd < 0)
		return (-ESTORE);

	// create new tmp file
	fd = mkstemp(tmpfile);
	if (fd < 0) {
		dbgerr("failed to create \"%s\" (%s)", tmpfile, identifier);
		return (-ESTORE);
	}

	if (posix_fallocate(fd, 0, count)) {
		dbgerr("couldn't fallocate \"%s\" (%s)", tmpfile, identifier);
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
		dbgerr("incomplete write or failed to sync: \"%s\" (%s)", tmpfile, identifier);
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
		dbgerr("failed to rename \"%s\" to \"%s\"", tmpfile, identifier);
		goto out;
	}

	dbgmsg(1, 1, "identifier: \"%s\", tmp: \"%s\", v: %d, sz: %zu", identifier, tmpfile, *version, size);

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
	ssize_t count = (ssize_t)size;
	int fd;
	char magic[ARRAY_SIZE(Storage_magic)];
	storage_version_t sversion = 0;

	// assert(size < SSIZE_MAX);
	
	if (!Storage_configured)
		return (-ENOTCONFIGURED);

	if (!identifier || !version || !object)
		return (-EINVALID);
	
	// open stream
	fd = open(identifier, O_RDONLY);
	if (fd < 0) {
		dbgmsg(1, 1, "failed to open \"%s\" for reading", identifier);
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

	dbgmsg(1, 1, "identifier: \"%s\", v: %d, sz: %zu", identifier, *version, size);

	return (ALL_OK);
}

/** Quick hack. @warning no other chdir should be performed */
int storage_config(void)
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

void storage_deconfig(void)
{
	Storage_configured = false;
	free((void *)Storage_path);
	Storage_path = NULL;
}
