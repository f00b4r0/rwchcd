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
#include <stdlib.h>	// free()

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
		struct sockaddr ai_addr;
		socklen_t ai_addrlen;
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

/**
 * Create the StatsD log database. NOP.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_statsd_create(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	return (ALL_OK);
}

/**
 * Update the StatsD log database.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 * @warning uses a static buffer: not thread safe
 */
static int log_statsd_update(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	static char buffer[LOG_STATSD_UDP_BUFSIZE];	// a static buffer is preferable to dynamic allocation for performance reasons
	const char * restrict mtype;
	int sockfd, ret;
	ssize_t sent;
	unsigned int i;

	assert(identifier && log_data);

	sockfd = log_statsd_udp_link();
	if (sockfd < 0)
		return (sockfd);

	for (i = 0; i < log_data->nvalues; i++) {
#ifdef DEBUG
		if ((ALL_OK != statsd_validate(log_data->keys[i]))) {
			dbgerr("invalid log key \"%s\"", log_data->keys[i]);
			continue;
		}
#endif

		switch (log_data->metrics[i]) {
			case LOG_METRIC_GAUGE:
				mtype = "g";
				break;
			case LOG_METRIC_COUNTER:
				mtype = "c";
				break;
			default:
				ret = -EINVALID;
				goto cleanup;
		}
		ret = snprintf(buffer, LOG_STATSD_UDP_BUFSIZE, "%s%s.%s:%d|%s\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], log_data->values[i], mtype);
		if ((ret < 0) || (ret >= (LOG_STATSD_UDP_BUFSIZE))) {
			ret = -ESTORE;
			goto cleanup;
		}

		sent = sendto(sockfd, buffer, ret, 0, &Log_statsd.run.ai_addr, Log_statsd.run.ai_addrlen);
		if (-1 == sent) {
			dbgerr("could not send");
			perror("log_statsd");
			goto cleanup;
		}

	}

	ret = ALL_OK;

cleanup:
	close(sockfd);

	return (ret);
}

void log_statsd_hook(struct s_log_bendcbs * restrict const callbacks)
{
	assert(callbacks);

	callbacks->backend = LOG_BKEND_STATSD;
	callbacks->log_create = log_statsd_create;
	callbacks->log_update = log_statsd_update;
}

void log_statsd_filecfg_dump(void)
{
	filecfg_iprintf("host \"%s\";\n", Log_statsd.set.host);	// mandatory
	filecfg_iprintf("port \"%s\";\n", Log_statsd.set.port);	// mandatory
	if (FCD_Exhaustive || Log_statsd.set.prefix)
		filecfg_iprintf("prefix \"%s\";\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "");	// optional
}

/**
 * Parse StatsD logging configuration.
 * @param priv unused
 * @param node a `backend "statsd"` node
 * @return exec status
 * @bug allocates memory that's never freed (needs init/exit callbacks for proper handling)
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

	if ((NODESTR != node->type) || strcmp("statsd", node->value.stringval) || (!node->children))
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
				break;
			default:
				break;	// should never happen
		}
	}

	return (ret);

fail:
	if (Log_statsd.set.host)
		free((void *)Log_statsd.set.host);
	if (Log_statsd.set.port)
		free((void *)Log_statsd.set.port);
	if (Log_statsd.set.prefix)
		free((void *)Log_statsd.set.prefix);

	return (-EOOM);
}

