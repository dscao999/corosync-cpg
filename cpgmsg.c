#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include "cpg_comm.h"
#include "loglog.h"
#include "squeue.h"

static volatile int finish = 0;

void sig_handler(int sig)
{
	if (sig == SIGTERM || sig == SIGINT)
		finish = 1;
}

static void msg_arrived(struct cpg_comm *cpg, uint32_t node,
		const void *msg, size_t len)
{
	const struct package_head *mp = msg;
	
	if (mp->confirm == 0) {
		printf("Node %d: %s\n", node, mp->msg);
		memcpy(cpg->ripemd, mp->ripemd, 20);
		cpg->confirm = 1;
		cpgcomm_write(cpg, cpg->ripemd, 21);
	} else
		queue_confirm(mp->ripemd);
}

int main(int argc, char *argv[])
{
	struct cpg_comm *cpg;
	int retv, llen, msglen;
	char *ln;
	struct sigaction sact;
	struct send_entry *entry;
	struct ripemd160 *ripe;


	openlog("cpgmsg", LOG_PERROR, LOG_DAEMON);

	ripe = ripemd160_init();
	cpg = cpgcomm_init("blockchain", msg_arrived);
	if (!cpg) {
		logmsg(LOG_ERR, "Cannot initialize blockchain\n");
		return 100;
	}
	retv = 0;

	sigaction(SIGINT, NULL, &sact);
	sact.sa_handler = sig_handler;
	sigaction(SIGINT, &sact, NULL);
	sigaction(SIGTERM, &sact, NULL);

	do {
		ln = readline("? ");
		if (*ln == 0) {
			finish = 1;
			free(ln);
			continue;
		}
		queue_tail_advance();
		llen = strlen(ln);
		msglen = sizeof(struct package_head) + llen + 1;
		entry = queue_entry_next();
		if (!entry)
			break;
		if (entry->bufhead == NULL) {
			entry->bufhead = malloc(msglen);
			entry->maxlen = msglen;
		} else if (entry->maxlen < msglen) {
			entry->bufhead = realloc(entry->bufhead, msglen);
			entry->maxlen = msglen;
		}
		entry->vote = cpg_numnodes(cpg) - 1;
		ripemd160_dgst(ripe, (const unsigned char *)ln, llen + 1);
		memcpy(entry->bufhead->ripemd, ripe->H, 20);
		ripemd160_reset(ripe);
		entry->bufhead->confirm = 0;
		memcpy(entry->bufhead->msg, ln, llen+1);
		cpgcomm_write(cpg, entry->bufhead, msglen);
		free(ln);
	} while (finish == 0);

	queue_free();
	cpgcomm_exit(cpg);
	ripemd160_exit(ripe);
	closelog();
	return retv;
}
