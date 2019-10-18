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

	msgbuf = malloc(4096);
	do {
		num = cpgcomm_read(cpg, &nodeid, msgbuf, 4096);
		if (num >= 4096)
			logmsg(LOG_ERR, "Message overflow!\n");
		else if (num > 0) {
			msgbuf[num] = 0;
			printf("Node: %d->%s\n", (int)nodeid, msgbuf);
		}
	} while (cpg->exflag == 0);
	free(msgbuf);
	return NULL;
}

int main(int argc, char *argv[])
{
	struct cpg_comm *cpg;
	pthread_t thid;
	int retv, sysret, nmb;
	char *ln, *buf;
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
	buf = malloc(4096);
	
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
		if (!fin) {
			cpgcomm_write(cpg, ln, strlen(ln));
		} else {
			strcpy(buf, "File: ");
			strcat(buf, ln);
			strcat(buf, "\n");
			cpgcomm_write(cpg, buf, strlen(buf));
			nmb = fread(buf, 1, 4096, fin);
			while (!feof(fin)) {
				cpgcomm_write(cpg, buf, nmb);
				nmb = fread(buf, 1, 4096, fin);
			}
			if (nmb > 0)
				cpgcomm_write(cpg, buf, nmb);
			fclose(fin);
		}
		free(ln);
	} while (finish == 0);

	free(buf);
	cpg->exflag = 1;
	pthread_join(thid, NULL);

exit_10:
	cpgcomm_exit(cpg);
	closelog();
	return retv;
}
