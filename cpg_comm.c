#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <poll.h>
#include <syslog.h>
#include <stdarg.h>
#include "cpg_comm.h"
#include "loglog.h"

struct msghead {
	uint32_t nodeid;
	unsigned len;
};

static void config_event(cpg_handle_t hd, const struct cpg_name *name,
		const struct cpg_address *mem, size_t memlen,
		const struct cpg_address *lem, size_t lemlen,
		const struct cpg_address *jem, size_t jemlen)
{
	int i, cpgret;
	const struct cpg_address *cm;
	struct cpg_comm *cpg;
	void *ptr;

	cpgret = cpg_context_get(hd, &ptr);
	cpg = ptr;
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_context_get failed: %d\n", cpgret);
		return;
	}
	assert(hd == cpg->hand);
	if (strncmp(cpg->group, name->value, strlen(cpg->group)) != 0)
		return;

	if (memlen > 0) {
		logmsg(LOG_NOTICE, "Current Members:");
		for (i = 0, cm = mem; i < memlen; i++, cm++)
			logmsg(LOG_NOTICE, " %d", (int)cm->nodeid);
		logmsg(LOG_NOTICE, " ");
	}
	if (lemlen > 0) {
		logmsg(LOG_NOTICE, "Members Left:");
		for (i = 0, cm = lem; i < lemlen; i++, cm++)
			logmsg(LOG_NOTICE, " %d", (int)cm->nodeid);
		logmsg(LOG_NOTICE, " ");
	}
	if (jemlen > 0) {
		logmsg(LOG_NOTICE, "Members Joined:");
		for (i = 0, cm = jem; i < jemlen; i++, cm++)
			logmsg(LOG_NOTICE, " %d", (int)cm->nodeid);
	}
	logmsg(LOG_NOTICE, "\n");
}

static void mesg_arrived(cpg_handle_t hd, const struct cpg_name *name,
		uint32_t nodeid, uint32_t pid, void *msg, size_t msglen)
{
	struct cpg_comm *cpg;
	int cpgret, sysret, pos;
	void *ptr;
	struct msghead head;

	cpgret = cpg_context_get(hd, &ptr);
	cpg = ptr;
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_context_get failed: %d\n", cpgret);
		return;
	}
	assert(hd == cpg->hand);
	if (nodeid == cpg->nodeid || name->length >= 32 ||
			msglen > CPG_CHUNK_SIZE ||
			memcmp(name->value, cpg->group, name->length) != 0)
		return;
	head.len = msglen;
	head.nodeid = nodeid;
	sysret = write(cpg->pin[1], &head, sizeof(struct msghead));
	if (unlikely(sysret == -1)) {
		logmsg(LOG_ERR, "pipe write failed: %d\n", errno);
		return;
	}
	assert(sysret == sizeof(struct msghead));
	pos = 0;
	while (pos < head.len) {
		sysret = write(cpg->pin[1], msg+pos, head.len - pos);
		if (unlikely(sysret == -1)) {
			logmsg(LOG_ERR, "pipe write failed: %d\n", errno);
			return;
		}
		pos += sysret;
	}
}

int cpgcomm_read(struct cpg_comm *cpg, uint32_t *nodeid, void *buf, int buflen)
{
	int sysret, clen, pos;
	struct pollfd pfd;
	struct msghead head;
	char *ignbuf;

	pfd.fd = cpg->pin[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	sysret = poll(&pfd, 1, 100);
	if (sysret == -1) {
		if (errno == EINTR)
			return 0;
		logmsg(LOG_ERR, "poll error: %d\n", errno);
		return 0;
	} else if (sysret == 0)
		return 0;
	assert((pfd.revents | POLLIN) != 0);

	sysret = read(cpg->pin[0], &head, sizeof(struct msghead));
	if (unlikely(sysret == -1)) {
		logmsg(LOG_ERR, "pipe read failed: %d\n", errno);
		return 0;
	}
	assert(sysret == sizeof(struct msghead));
	*nodeid = head.nodeid;
	clen = (head.len > buflen)? buflen : head.len;
	pos = 0;
	while (pos < clen) {
		sysret = read(cpg->pin[0], buf+pos, clen - pos);
		if (unlikely(sysret == -1)) {
			logmsg(LOG_ERR, "pipe read failed: %d\n", errno);
			return 0;
		}
		pos += sysret;
	}
	if (unlikely(pos < head.len)) {
		logmsg(LOG_WARNING, "cpgcomm_read buffer too small!\n");
		ignbuf = malloc(head.len - pos);
		if (!ignbuf) {
			logmsg(LOG_CRIT, "Out of Memory!\n");
			exit(100);
		}
		while (pos < head.len) {
			sysret = read(cpg->pin[0], ignbuf, head.len - pos);
			if (unlikely(sysret == -1)) {
				logmsg(LOG_ERR, "pipe read failed: %d\n", errno);
				break;
			}
			pos += sysret;
		}
		free(ignbuf);
	}
	return head.len;
}

void cpgcomm_write(struct cpg_comm *cpg, const void *msg, int len)
{
	int cpgret, count;
	static const struct timespec tm = {.tv_sec = 0, .tv_nsec = 200000000};

	if (unlikely(len > CPG_CHUNK_SIZE)) {
		logmsg(LOG_ERR, "Block size: %d exceeds limit.\n", len);
		return;
	}
	count = 0;
	cpg->iovec.iov_base = malloc(len);
	cpg->iovec.iov_len =  len;
	memcpy(cpg->iovec.iov_base, msg, len);
	cpgret = cpg_mcast_joined(cpg->hand, CPG_TYPE_FIFO, &cpg->iovec, 1);
	while (cpgret == CS_ERR_TRY_AGAIN && count < 10) {
		count++;
		nanosleep(&tm, NULL);
	}
	if (unlikely(cpgret != CS_OK))
		logmsg(LOG_ERR, "cpg_mcast_joined failed: %d\n", cpgret);
	free(cpg->iovec.iov_base);
}

static void *watch_mesg(void *arg)
{
	struct cpg_comm *cpg = arg;
	static const struct timespec tm = {.tv_sec = 0, .tv_nsec = 10000000};
	int cpgret;

	do {
		cpgret = cpg_dispatch(cpg->hand, CS_DISPATCH_ALL);
		if (unlikely(cpgret != CS_OK))
			logmsg(LOG_ERR, "cpg_dispatch failed: %d\n", cpgret);
		nanosleep(&tm, NULL);
	} while (cpg->exflag == 0);
	return NULL;
}

struct cpg_comm *cpgcomm_init(const char *gname)
{
	struct cpg_comm *cpg;
	struct cpg_name gr;
	cpg_callbacks_t trigger;
	int sysret, cpgret;

	gr.length = strlen(gname);
	if (gr.length > 31)
		return NULL;
	cpg = malloc(sizeof(struct cpg_comm));
	if (!cpg) {
		logmsg(LOG_CRIT, "No Enough Memory!\n");
		goto exit_10;
	}
	cpg->exflag = 0;
	strcpy(cpg->group, gname);
	sysret = pipe(cpg->pin);
	if (unlikely(sysret == -1)) {
		logmsg(LOG_ERR, "pipe creation failed: %d\n", errno);
		goto exit_20;
	}
	trigger.cpg_deliver_fn = mesg_arrived;
	trigger.cpg_confchg_fn = config_event;
	cpgret = cpg_initialize(&cpg->hand, &trigger);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_initialize failed: %d\n", cpgret);
		goto exit_30;
	}
	cpgret = cpg_context_set(cpg->hand, cpg);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_context_set failed: %d\n", cpgret);
		goto exit_40;
	}
	cpg_local_get(cpg->hand, &cpg->nodeid);
	memcpy(gr.value, gname, gr.length);
	cpgret = cpg_join(cpg->hand, &gr);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_join failed: %d\n", cpgret);
		goto exit_40;
	}
	sysret = pthread_create(&cpg->thid, NULL, watch_mesg, cpg);
	if (unlikely(sysret != 0)) {
		logmsg(LOG_ERR, "pthread create failed: %d\n", sysret);
		goto exit_50;
	}
	return cpg;

exit_50:
	cpg_leave(cpg->hand, &gr);
exit_40:
	cpg_finalize(cpg->hand);
exit_30:
	close(cpg->pin[0]);
	close(cpg->pin[1]);
exit_20:
	free(cpg);
	cpg = NULL;
exit_10:
	return cpg;
}

void cpgcomm_exit(struct cpg_comm *cpg)
{
	struct cpg_name gr;
	int cpgret;

	cpg->exflag = 1;
	gr.length = strlen(cpg->group);
	memcpy(gr.value, cpg->group, gr.length);
	cpgret = cpg_leave(cpg->hand, &gr);
	if (unlikely(cpgret != CS_OK))
		logmsg(LOG_ERR, "cpg leave failed: %d\n", cpgret);
	pthread_join(cpg->thid, NULL);
	cpg_finalize(cpg->hand);
	close(cpg->pin[0]);
	close(cpg->pin[1]);
	free(cpg);
}
