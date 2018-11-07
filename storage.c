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
#include <errno.h>	// errno
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>	// open/posix_fallocate/fadvise
#include <stdlib.h>	// mkstemp/malloc

#include "storage.h"
#include "log_file.h"
#include "log_rrd.h"

#define STORAGE_MAGIC		"rwchcd"
#define STORAGE_VERSION		1UL
#define STORAGE_TMPLATE		"tmpXXXXXX"

static const char Storage_magic[] = STORAGE_MAGIC;
static const storage_version_t Storage_version = STORAGE_VERSION;

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
		dbgmsg("failed to create \"%s\" (%s)", tmpfile, identifier);
		return (-ESTORE);
	}

	if (posix_fallocate(fd, 0, count)) {
		dbgmsg("couldn't fallocate \"%s\" (%s)", tmpfile, identifier);
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
		dbgmsg("incomplete write or failed to sync: \"%s\" (%s)", tmpfile, identifier);
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
		dbgmsg("failed to rename \"%s\" to \"%s\"", tmpfile, identifier);
		goto out;
	}

	dbgmsg("identifier: \"%s\", tmp: \"%s\", v: %d, sz: %zu", identifier, tmpfile, *version, size);

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
		dbgmsg("failed to open \"%s\" for reading", identifier);
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

	dbgmsg("identifier: \"%s\", v: %d, sz: %zu", identifier, *version, size);

	return (ALL_OK);
}

enum e_storage_bend {
	SBEND_FILE,
	SBEND_RRD,
};

/**
 * Generic storage backend keys/values log call.
 * @param identifier a unique string identifying the data to log
 * @param version a caller-defined version number
 */
int storage_log(const char * restrict const identifier, const storage_version_t * restrict const version, const struct s_log_data * restrict const log_data)
{
	storage_version_t lversion = 0;
	const enum e_storage_bend bend = (log_data->interval > 0) ? SBEND_RRD : SBEND_FILE;	// XXX hack: enable SBEND_RRD when interval is a strictly positive value
	bool fcreate = false;
	char *fmtfile;
	int ret;
	struct {
		unsigned int nkeys;
		unsigned int nvalues;
		int interval;
		enum e_storage_bend bend;
	} logfmt;
	
	if (!identifier || !version || !log_data)
		return (-EINVALID);

	if (log_data->nvalues > log_data->nkeys)
		return (-EINVALID);

	fmtfile = malloc(strlen(identifier) + strlen(".fmt") + 1);
	if (!fmtfile)
		return (-EOOM);

	strcpy(fmtfile, identifier);
	strcat(fmtfile, ".fmt");

	ret = storage_fetch(fmtfile, &lversion, &logfmt, sizeof(logfmt));
	if (ALL_OK != ret)
		fcreate = true;
	else {
		// compare with current local version
		if (lversion != *version)
			fcreate = true;

		// compare with current number of keys
		if (logfmt.nkeys != log_data->nkeys)
			fcreate = true;

		// compare with current backend
		if (logfmt.bend != bend)
			fcreate = true;
	}

	if (fcreate) {
		// make sure we're in target wd
		if (chdir(RWCHCD_STORAGE_PATH)) {
			ret = -ESTORE;
			goto cleanup;
		}

		// create backend store
		switch (bend) {
			case SBEND_FILE:
				ret = log_file_create(identifier, log_data);
				break;
			case SBEND_RRD:
				ret = log_rrd_create(identifier, log_data);
				break;
			default:
				ret = -EINVALID;
		}

		if (ALL_OK != ret)
			goto cleanup;

		// register new format
		logfmt.nkeys = log_data->nkeys;
		logfmt.nvalues = log_data->nvalues;
		logfmt.interval = log_data->interval;
		logfmt.bend = bend;
		ret = storage_dump(fmtfile, version, &logfmt, sizeof(logfmt));
		if (ALL_OK != ret)
			goto cleanup;
	}

	// make sure we're in target wd
	if (chdir(RWCHCD_STORAGE_PATH)) {
		ret = -ESTORE;
		goto cleanup;
	}

	// log data
	switch (bend) {
		case SBEND_FILE:
			ret = log_file_update(identifier, log_data);
			break;
		case SBEND_RRD:
			ret = log_rrd_update(identifier, log_data);
			break;
		default:
			ret = -EINVALID;
	}

cleanup:
	free(fmtfile);
	return (ret);
}
