//
//  log_statsd.c
//  rwchcd
//
//  (C) 2019 Thibaut VARENE
//  License: GPLv2 - http://www.gnu.org/licenses/gpl-2.0.html
//

/**
 * @file
 * StatsD log implementation.
 * https://github.com/statsd/statsd/wiki
 * @note undocumented protocol makes everything more fun. Not.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <unistd.h>	// close()
#include <string.h>	// memcpy/memset
#include <assert.h>
#include <stdlib.h>	// malloc/free()

#include "log_statsd.h"
#include "rwchcd.h"
#include "filecfg.h"
#include "filecfg_parser.h"

#define LOG_STATSD_UDP_BUFSIZE	1432	///< udp buffer size. Untold rule seems to be that the datagram must not be fragmented.

static struct {
	struct {
		const char * restrict host;	///< statsd host address (hostname or IP, as a string, e.g. `"localhost"`)
		const char * restrict port;	///< statsd host port or service (as a string, e.g. `"3456"`)
		const char * restrict prefix;	///< statsd namespace prefix (dot-terminated)
	} set;
	struct {
		bool online;			///< true if backend is online
		struct sockaddr ai_addr;
		socklen_t ai_addrlen;
		int sockfd;
	} run;
} Log_statsd = {
	.set.host = NULL,
	.set.port = NULL,
	.set.prefix = NULL,
};

/**
 * Resolve remote host and open socket.
 * @return socket fd or negative error
 */
static int log_statsd_udp_link(void)
{
	int sockfd;
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int ret;

	// obtain address(es) matching host/port
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	ret = getaddrinfo(Log_statsd.set.host, Log_statsd.set.port, &hints, &result);
	if (ret) {
		dbgerr("getaddrinfo: %s\n", gai_strerror(ret));
		return (-ESTORE);
	}

	// try each address until one succeeds
	for (rp = result; rp; rp = rp->ai_next) {
		sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (-1 == sockfd)
			continue;
		else
			break;	// success
	}

	if (!rp) {
		dbgerr("Could not reach server\n");
		ret = -ESTORE;
		goto cleanup;
	}

	memcpy(&Log_statsd.run.ai_addr, &rp->ai_addr, sizeof(Log_statsd.run.ai_addr));
	Log_statsd.run.ai_addrlen = rp->ai_addrlen;

	ret = sockfd;

cleanup:
	freeaddrinfo(result);

	return (ret);
}

static int statsd_validate(const char * stat)
{
	const char * p;
	for (p = stat; *p; p++) {
		switch (*p) {
			case ':':
			case '|':
			case '@':
				return (-EINVALID);
			default:
				;	// nothing
		}
	}

	return (ALL_OK);
}

/** Online the StatsD log backend. */
static int log_statsd_online(void)
{
	int sockfd;

	// check we're ready to fly
	if (!Log_statsd.set.host || !Log_statsd.set.port) {
		pr_err("Misconfigured StatsD backend");
		return (-EMISCONFIGURED);
	}

	sockfd = log_statsd_udp_link();
	if (sockfd < 0)
		return (sockfd);

	Log_statsd.run.sockfd = sockfd;
	Log_statsd.run.online = true;

	return (ALL_OK);
}

/** Offline the StatsD log backend. */
static void log_statsd_offline(void)
{
	close (Log_statsd.run.sockfd);

	if (Log_statsd.set.host)
		free((void *)Log_statsd.set.host);
	if (Log_statsd.set.port)
		free((void *)Log_statsd.set.port);
	if (Log_statsd.set.prefix)
		free((void *)Log_statsd.set.prefix);

	memset(&Log_statsd, 0, sizeof(Log_statsd));
}

/**
 * Create the StatsD log database. NOP.
 * @param async true if called asynchronously
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_statsd_create(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data)
{
	if (!Log_statsd.run.online)
		return (-EOFFLINE);

	return (ALL_OK);
}

/**
 * Update the StatsD log database.
 * @param async true if called asynchronously
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 * @todo improve performance by grouping data (look into sendmsg()).
 */
static int log_statsd_update(const bool async, const char * restrict const identifier, const struct s_log_data * const log_data)
{
	static char sbuffer[LOG_STATSD_UDP_BUFSIZE];	// a static buffer is preferable to dynamic allocation for performance reasons
	char * restrict buffer;
	bool zerofirst;
	char mtype;
	int ret;
	ssize_t sent;
	unsigned int i;

	assert(identifier && log_data);

	if (!Log_statsd.run.online)
		return (-EOFFLINE);

	// don't use the static buffer when called asynchronously
	if (async) {
		buffer = malloc(LOG_STATSD_UDP_BUFSIZE);
		if (!buffer)
			return (-EOOM);
	}
	else
		buffer = sbuffer;

	for (i = 0; i < log_data->nvalues; i++) {
#ifdef DEBUG
		if ((ALL_OK != statsd_validate(log_data->keys[i]))) {
			dbgerr("invalid \"%s\" log key \"%s\"", identifier, log_data->keys[i]);
			continue;
		}
#endif

		ret = 0;
		zerofirst = false;

		switch (log_data->metrics[i]) {
			case LOG_METRIC_GAUGE:
				mtype = 'g';
				if (log_data->values[i] < 0)
					zerofirst = true;
				break;
			case LOG_METRIC_COUNTER:
				mtype = 'c';
				break;
			default:
				ret = -EINVALID;
				goto cleanup;
		}

		// StatsD has a schizophrenic idea of what a gauge is (negative values are subtracted from previous data and not registered as is): work around its dementia
		if (zerofirst) {
			ret = snprintf(buffer, LOG_STATSD_UDP_BUFSIZE, "%s%s.%s:0|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], mtype);
			if ((ret < 0) || (ret >= (LOG_STATSD_UDP_BUFSIZE))) {
				ret = -ESTORE;
				goto cleanup;
			}
		}
		ret = snprintf(buffer + ret, LOG_STATSD_UDP_BUFSIZE - ret, "%s%s.%s:%d|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], log_data->values[i], mtype);
		if ((ret < 0) || (ret >= (LOG_STATSD_UDP_BUFSIZE))) {
			ret = -ESTORE;
			goto cleanup;
		}

		sent = sendto(Log_statsd.run.sockfd, buffer, ret, 0, &Log_statsd.run.ai_addr, Log_statsd.run.ai_addrlen);
		if (-1 == sent) {
			dbgerr("could not send");
			perror("log_statsd");
			goto cleanup;
		}

	}

	ret = ALL_OK;

cleanup:
	if (async)
		free(buffer);

	return (ret);
}

void log_statsd_hook(struct s_log_bendcbs * restrict const callbacks)
{
	assert(callbacks);

	callbacks->bkid = LOG_BKEND_STATSD;
	callbacks->separator = '.';
	callbacks->log_online = log_statsd_online;
	callbacks->log_offline = log_statsd_offline;
	callbacks->log_create = log_statsd_create;
	callbacks->log_update = log_statsd_update;
}

void log_statsd_filecfg_dump(void)
{
	if (!Log_statsd.run.online)
		return;

	filecfg_iprintf("host \"%s\";\n", Log_statsd.set.host);	// mandatory
	filecfg_iprintf("port \"%s\";\n", Log_statsd.set.port);	// mandatory
	if (FCD_Exhaustive || Log_statsd.set.prefix)
		filecfg_iprintf("prefix \"%s\";\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "");	// optional
}

/**
 * Parse StatsD logging configuration.
 * @param priv unused
 * @param node a `backend "#LOG_BKEND_STATSD_NAME"` node
 * @return exec status
 */
int log_statsd_filecfg_parse(void * restrict const priv, const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "host", true, NULL, NULL, },		// 0
		{ NODESTR, "port", true, NULL, NULL, },
		{ NODESTR, "prefix", false, NULL, NULL, },	// 2
	};
	const struct s_filecfg_parser_node * currnode;
	unsigned int i;
	int ret;

	if ((NODESTR != node->type) || strcmp(LOG_BKEND_STATSD_NAME, node->value.stringval) || (!node->children))
		return (-EINVALID);	// we only accept NODESTR node with children

	ret = filecfg_parser_match_nodechildren(node, parsers, ARRAY_SIZE(parsers));
	if (ALL_OK != ret)
		return (ret);	// break if invalid config

	// reset config
	memset(&Log_statsd, 0, sizeof(Log_statsd));

	for (i = 0; i < ARRAY_SIZE(parsers); i++) {
		currnode = parsers[i].node;
		if (!currnode)
			continue;

		ret = -EOOM;
		switch (i) {
			case 0:
				Log_statsd.set.host = strdup(currnode->value.stringval);
				if (!Log_statsd.set.host)
					goto fail;
				break;
			case 1:
				Log_statsd.set.port = strdup(currnode->value.stringval);
				if (!Log_statsd.set.port)
					goto fail;
				break;
			case 2:
				Log_statsd.set.prefix = strdup(currnode->value.stringval);
				if (!Log_statsd.set.prefix)
					goto fail;
				else if ('.' != Log_statsd.set.prefix[strlen(Log_statsd.set.prefix)-1]) {
					filecfg_parser_pr_err(_("Missing ending '.' in prefix \"%s\" closing at line %d"), currnode->value.stringval, currnode->lineno);
					ret = -EMISCONFIGURED;
					goto fail;
				}
				break;
			default:
				break;	// should never happen
		}
	}

	if (!Log_statsd.set.host || !Log_statsd.set.port) {
		filecfg_parser_pr_err(_("In node \"%s\" closing at line %d: missing host or port"), node->name, node->lineno);
		ret = -EMISCONFIGURED;
		goto fail;
	}

	return (ret);

fail:
	if (Log_statsd.set.host)
		free((void *)Log_statsd.set.host);
	if (Log_statsd.set.port)
		free((void *)Log_statsd.set.port);
	if (Log_statsd.set.prefix)
		free((void *)Log_statsd.set.prefix);

	return (ret);
}

