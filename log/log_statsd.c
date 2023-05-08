//
//  log/log_statsd.c
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
#include "filecfg/dump/filecfg_dump.h"
#include "filecfg/parse/filecfg_parser.h"

#define LOG_STATSD_UDP_BUFSIZE	1432	///< udp buffer size. Untold rule seems to be that the datagram must not be fragmented.

static struct {
	struct {
		const char * restrict host;	///< statsd host address (hostname or IP, as a string, e.g. `"localhost"`)
		const char * restrict port;	///< statsd host port or service (as a string, e.g. `"3456"`)
		const char * restrict prefix;	///< statsd namespace prefix (dot-terminated)
	} set;
	struct {
		bool online;			///< true if backend is online
		struct sockaddr_storage ai_addr;
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
		if (-1 != sockfd)
			break;	// success
	}

	if (!rp) {
		dbgerr("Could not reach server\n");
		ret = -ESTORE;
		goto cleanup;
	}

	memcpy(&Log_statsd.run.ai_addr, rp->ai_addr, rp->ai_addrlen);	// ai_addrlen is guaranteed to be <= sizeof(sockaddr_storage)
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
	memset(&Log_statsd.run, 0, sizeof(Log_statsd.run));
}

/** Cleanup the StatsD log backend. */
static void log_statsd_cleanup(void)
{
	freeconst(Log_statsd.set.host);
	freeconst(Log_statsd.set.port);
	freeconst(Log_statsd.set.prefix);
}

/**
 * Create the StatsD log database. NOP.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 */
static int log_statsd_create(const char * restrict const identifier __attribute__((unused)), const struct s_log_data * const log_data __attribute__((unused)))
{
	if (!Log_statsd.run.online)
		return (-EOFFLINE);

	return (ALL_OK);
}

/**
 * Update the StatsD log database.
 * @param identifier the database identifier
 * @param log_data the data to be logged
 * @return exec status
 * @warning not thread-safe.
 */
static int log_statsd_update(const char * restrict const identifier, const struct s_log_data * const log_data)
{
	static char sbuffer[LOG_STATSD_UDP_BUFSIZE];
	char * buffer;
	bool zerofirst;
	char mtype;
	int ret;
	ssize_t sent;
	size_t avail;
	unsigned int i;

	assert(identifier && log_data);

	if (!Log_statsd.run.online)
		return (-EOFFLINE);

	buffer = sbuffer;
	avail = LOG_STATSD_UDP_BUFSIZE;

	for (i = 0; i < log_data->nvalues; i++) {
#ifdef DEBUG
		if ((ALL_OK != statsd_validate(log_data->keys[i]))) {
			dbgerr("invalid \"%s\" log key \"%s\"", identifier, log_data->keys[i]);
			continue;
		}
#endif

		zerofirst = false;

		switch (log_data->metrics[i]) {
			case LOG_METRIC_IGAUGE:
				mtype = 'g';
				if (log_data->values[i].i < 0)
					zerofirst = true;
				break;
			case LOG_METRIC_FGAUGE:
				mtype = 'g';
				if (log_data->values[i].f < 0.0F)
					zerofirst = true;
				break;
			case LOG_METRIC_ICOUNTER:
			case LOG_METRIC_FCOUNTER:
				mtype = 'c';
				break;
			default:
				ret = -EINVALID;
				goto cleanup;
		}

restartzero:
		// StatsD has a schizophrenic idea of what a gauge is (negative values are subtracted from previous data and not registered as is): work around its dementia
		if (zerofirst) {
			ret = snprintf(buffer, avail, "%s%s.%s:0|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], mtype);
			assert(ret < LOG_STATSD_UDP_BUFSIZE);
			if (ret < 0) {
				ret = -ESTORE;
				goto cleanup;
			}
			else if ((size_t)ret >= avail) {
				// send what we have, reset buffer, restart - no need to add '\0': sendto will truncate anyway
				sendto(Log_statsd.run.sockfd, sbuffer, LOG_STATSD_UDP_BUFSIZE - avail, 0, (struct sockaddr *)&Log_statsd.run.ai_addr, Log_statsd.run.ai_addrlen);
				buffer = sbuffer;
				avail = LOG_STATSD_UDP_BUFSIZE;
				goto restartzero;
			}
			buffer += ret;
			avail -= (size_t)ret;
		}

restartbuffer:
		switch (log_data->metrics[i]) {
			case LOG_METRIC_IGAUGE:
				ret = snprintf(buffer, avail, "%s%s.%s:%d|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], log_data->values[i].i, mtype);
				break;
			case LOG_METRIC_ICOUNTER:
				ret = snprintf(buffer, avail, "%s%s.%s:%u|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], log_data->values[i].u, mtype);
				break;
			case LOG_METRIC_FGAUGE:
			case LOG_METRIC_FCOUNTER:
				ret = snprintf(buffer, avail, "%s%s.%s:%f|%c\n", Log_statsd.set.prefix ? Log_statsd.set.prefix : "", identifier, log_data->keys[i], log_data->values[i].f, mtype);
				break;
			default:
				ret = 0;
				break;	// cannot happen thanks to previous switch()
		}

		assert(ret < LOG_STATSD_UDP_BUFSIZE);
		if (ret < 0) {
			ret = -ESTORE;
			goto cleanup;
		}
		else if ((size_t)ret >= avail) {
			// send what we have, reset buffer, restart - no need to add '\0': sendto will truncate anyway
			sendto(Log_statsd.run.sockfd, sbuffer, LOG_STATSD_UDP_BUFSIZE - avail, 0, (struct sockaddr *)&Log_statsd.run.ai_addr, Log_statsd.run.ai_addrlen);
			buffer = sbuffer;
			avail = LOG_STATSD_UDP_BUFSIZE;
			goto restartbuffer;
		}
		buffer += ret;
		avail -= (size_t)ret;
	}

	ret = ALL_OK;

cleanup:
	// we only check for sendto() errors here
	sent = sendto(Log_statsd.run.sockfd, sbuffer, LOG_STATSD_UDP_BUFSIZE - avail, 0, (struct sockaddr *)&Log_statsd.run.ai_addr, Log_statsd.run.ai_addrlen);
	if (-1 == sent) {
		dbgerr("could not send");
		perror("log_statsd");
	}
	return (ret);
}

static const struct s_log_bendcbs log_statsd_cbs = {
	.bkid		= LOG_BKEND_STATSD,
	.unversioned	= true,
	.separator	= '.',
	.log_online	= log_statsd_online,
	.log_offline	= log_statsd_offline,
	.log_cleanup	= log_statsd_cleanup,
	.log_create	= log_statsd_create,
	.log_update	= log_statsd_update,
};

void log_statsd_hook(const struct s_log_bendcbs ** restrict const callbacks)
{
	assert(callbacks);
	*callbacks = &log_statsd_cbs;
}

#ifdef HAS_FILECFG
void log_statsd_filecfg_dump(void)
{
	if (!Log_statsd.run.online)
		return;

	filecfg_dump_nodestr("host", Log_statsd.set.host);	// mandatory
	filecfg_dump_nodestr("port", Log_statsd.set.port);	// mandatory
	if (FCD_Exhaustive || Log_statsd.set.prefix)
		filecfg_dump_nodestr("prefix", Log_statsd.set.prefix ? Log_statsd.set.prefix : "");	// optional
}

/**
 * Parse StatsD logging configuration.
 * @param priv unused
 * @param node a `backend "#LOG_BKEND_STATSD_NAME"` node
 * @return exec status
 */
int log_statsd_filecfg_parse(void * restrict const priv __attribute__((unused)), const struct s_filecfg_parser_node * const node)
{
	struct s_filecfg_parser_parsers parsers[] = {
		{ NODESTR, "host", true, NULL, NULL, },		// 0
		{ NODESTR, "port", true, NULL, NULL, },
		{ NODESTR, "prefix", false, NULL, NULL, },	// 2
	};
	const struct s_filecfg_parser_node * currnode;
	unsigned int i;
	int ret;

	if ((NODESTC != node->type) || strcmp(LOG_BKEND_STATSD_NAME, node->value.stringval) || (!node->children))
		return (-EINVALID);	// we only accept NODESTC node with children

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

	return (ALL_OK);

fail:
	log_statsd_cleanup();
	return (ret);
}
#endif	/* HAS_FILECFG */
