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
#include <readline/readline.h>
#include <readline/history.h>
#include "cpg_comm.h"
#include "loglog.h"

#define unlikely(x)	__builtin_expect(x, 0)
#define CHUNK_SIZE	8192

static volatile int finish = 0;

void sig_handler(int sig)
{
	if (sig == SIGTERM || sig == SIGINT)
		finish = 1;
}

static void *watch_mesg(void *arg)
{
	struct cpg_comm *cpg = arg;
	char *msgbuf;
	int num;
	uint32_t nodeid;

	msgbuf = malloc(CHUNK_SIZE);
	do {
		num = cpgcomm_read(cpg, &nodeid, msgbuf, CHUNK_SIZE);
		if (num >= CHUNK_SIZE)
			logmsg(LOG_ERR, "Message overflow!\n");
		else if (num > 0) {
			msgbuf[num] = 0;
			printf("Node: %d->%s\n", (int)nodeid, msgbuf);
		}
	} while (cpg->exflag == 0);
	free(msgbuf);
	return NULL;
}

static int send_file(FILE *fin, struct cpg_comm *cpg)
{
	int nmb;
	unsigned int len;
	char *buf;

	len = 0;
	buf = malloc(CHUNK_SIZE);
	if (!buf) {
		logmsg(LOG_CRIT, "Out of Memory!\n");
		return 0;
	}
	do {
		nmb = fread(buf, 1, CHUNK_SIZE, fin);
		if (unlikely(ferror(fin)))
			logmsg(LOG_ERR, "fread error: %d\n", errno);
		if (nmb > 0)
			cpgcomm_write(cpg, buf, nmb);
		len += nmb;
	} while (!feof(fin));
	free(buf);

	return len;
}

int main(int argc, char *argv[])
{
	struct cpg_comm *cpg;
	pthread_t thid;
	int retv, sysret;
	char *ln;
	FILE *fin;
	struct sigaction sact;

	openlog("cpgmsg", LOG_PERROR, LOG_DAEMON);

	cpg = cpgcomm_init("blockchain");
	if (!cpg) {
		logmsg(LOG_ERR, "Cannot initialize blockchain\n");
		return 100;
	}
	retv = 0;

	sysret = pthread_create(&thid, NULL, watch_mesg, (void *)cpg);
	if (sysret) {
		logmsg(LOG_ERR, "pthread_create failed: %d\n", sysret);
		retv = 3;
		goto exit_10;
	}
	
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
		fin = fopen(ln, "rb");
		cpgcomm_write(cpg, ln, strlen(ln));
		if (fin) {
			send_file(fin, cpg);
			fclose(fin);
		}

	} while (finish == 0);

	cpg->exflag = 1;
	pthread_join(thid, NULL);

exit_10:
	cpgcomm_exit(cpg);
	closelog();
	return retv;
}
