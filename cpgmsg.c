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

static volatile int finish = 0;

void sig_handler(int sig)
{
	if (sig == SIGTERM || sig == SIGINT)
		finish = 1;
}

struct pkthead {
	char fname[64];
	char msg[0];
};

static void msg_arrived(uint32_t node, const void *msg, size_t len)
{
	const struct pkthead *mp = msg;

	if (mp->fname[0] == 0)
		printf("Node %d: %s\n", node, mp->msg);
}

int main(int argc, char *argv[])
{
	struct cpg_comm *cpg;
	int retv, llen, msglen;
	char *ln, *buf;
	struct sigaction sact;

	openlog("cpgmsg", LOG_PERROR, LOG_DAEMON);

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
		llen = strlen(ln);
		msglen = sizeof(struct pkthead) + llen + 1;
		buf = malloc(msglen);
		memset(buf, 0, sizeof(struct pkthead));
		memcpy(buf+sizeof(struct pkthead), ln, llen+1);
		cpgcomm_write(cpg, buf, msglen);
		free(buf);
		free(ln);
	} while (finish == 0);

	cpg->exflag = 1;

	cpgcomm_exit(cpg);
	closelog();
	return retv;
}
