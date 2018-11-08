//
//  log_file.c
//  rwchcd
//
//  (C) 2018 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * File log implementation.
 */

#include <stdio.h>	// fopen...
#include <time.h>	// time
#include <errno.h>	// errno
#include <assert.h>

#include "log_file.h"

/**
 * Create the log file (text file).
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
int log_file_create(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	FILE * restrict file = NULL;
	unsigned int i;

	assert(identifier && log_data);

	file = fopen(identifier, "w");	// create/truncate
	if (!file)
		return (-ESTORE);

	// write csv header
	fprintf(file, "time;");
	for (i = 0; i < log_data->nkeys; i++)
		fprintf(file, "%s;", log_data->keys[i]);
	fprintf(file, "\n");

	fclose(file);

	return (ALL_OK);
}

/**
 * Update the log file (text file).
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
int log_file_update(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	FILE * restrict file = NULL;
	unsigned int i;

	assert(identifier && log_data);

	file = fopen(identifier, "r+");	// r+w, do not create if not exist
	if (!file)
		return (-ESTORE);

	if(fseek(file, 0, SEEK_END))	// append
		return (-ESTORE);

	// write csv data
	fprintf(file, "%ld;", time(NULL));
	for (i = 0; i < log_data->nvalues; i++)
		fprintf(file, "%d;", log_data->values[i]);
	for (i = (log_data->nkeys - log_data->nvalues); i; i--)
		fprintf(file, ";");
	fprintf(file, "\n");

	// finally close the file
	fclose(file);

	return (ALL_OK);
}


void log_file_hook(struct s_log_callbacks * restrict const callbacks)
{
	assert(callbacks);

	callbacks->backend = SBEND_FILE;
	callbacks->log_create = log_file_create;
	callbacks->log_update = log_file_update;
}
