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
	int cpgret;
	void *ptr;

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
	if (cpg->rcvmsg)
		cpg->rcvmsg(nodeid, msg, msglen);
}

void cpgcomm_write(struct cpg_comm *cpg, void *msg, int len)
{
	int cpgret, count;
	static const struct timespec tm = {.tv_sec = 0, .tv_nsec = 200000000};

	if (unlikely(len > CPG_CHUNK_SIZE)) {
		logmsg(LOG_ERR, "Block size: %d exceeds limit.\n", len);
		return;
	}
	count = 0;
	cpg->iovec.iov_base = msg;
	cpg->iovec.iov_len =  len;
//	memcpy(cpg->iovec.iov_base, msg, len);
	cpgret = cpg_mcast_joined(cpg->hand, CPG_TYPE_FIFO, &cpg->iovec, 1);
	while (cpgret == CS_ERR_TRY_AGAIN && count < 10) {
		count++;
		cpgret = cpg_mcast_joined(cpg->hand, CPG_TYPE_FIFO,
				&cpg->iovec, 1);
		nanosleep(&tm, NULL);
	}
	if (unlikely(cpgret != CS_OK))
		logmsg(LOG_ERR, "cpg_mcast_joined failed: %d\n", cpgret);
}

static void *watch_mesg(void *arg)
{
	struct cpg_comm *cpg = arg;
	int cpgret;

	do {
		cpgret = cpg_dispatch(cpg->hand, CS_DISPATCH_ONE);
		if (unlikely(cpgret != CS_OK))
			logmsg(LOG_ERR, "cpg_dispatch failed: %d\n", cpgret);
	} while (cpg->exflag == 0);
	cpg->dispatching = 0;
	return NULL;
}

struct cpg_comm *cpgcomm_init(const char *gname,
		void (*rcvmsg)(uint32_t node, const void * msg, size_t len))
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
	memset(cpg, 0, sizeof(struct cpg_comm));
	strcpy(cpg->group, gname);
	trigger.cpg_deliver_fn = mesg_arrived;
	trigger.cpg_confchg_fn = config_event;
	cpgret = cpg_initialize(&cpg->hand, &trigger);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_initialize failed: %d\n", cpgret);
		goto exit_20;
	}
	cpgret = cpg_context_set(cpg->hand, cpg);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_context_set failed: %d\n", cpgret);
		goto exit_30;
	}
	cpg_local_get(cpg->hand, &cpg->nodeid);
	cpg->nodon[cpg->nodeid] = 1;
	cpg->rcvmsg = rcvmsg;
	memcpy(gr.value, gname, gr.length);
	cpgret = cpg_join(cpg->hand, &gr);
	if (unlikely(cpgret != CS_OK)) {
		logmsg(LOG_ERR, "cpg_join failed: %d\n", cpgret);
		goto exit_30;
	}
	cpg->dispatching = 1;
	sysret = pthread_create(&cpg->thid, NULL, watch_mesg, cpg);
	if (unlikely(sysret != 0)) {
		cpg->dispatching = 0;
		logmsg(LOG_ERR, "pthread create failed: %d\n", sysret);
		goto exit_40;
	}
	return cpg;

exit_40:
	cpg_leave(cpg->hand, &gr);
exit_30:
	cpg_finalize(cpg->hand);
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
	static const struct timespec tm = {0, 100000000};

	cpg->exflag = 1;
	gr.length = strlen(cpg->group);
	memcpy(gr.value, cpg->group, gr.length);
	cpgret = cpg_leave(cpg->hand, &gr);
	while (cpg->dispatching)
		nanosleep(&tm, NULL);
	if (unlikely(cpgret != CS_OK))
		logmsg(LOG_ERR, "cpg leave failed: %d\n", cpgret);
	pthread_join(cpg->thid, NULL);
	cpg_finalize(cpg->hand);
	free(cpg);
}
