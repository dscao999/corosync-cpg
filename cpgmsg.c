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
#include "loglog.h"
#include "squeue.h"

static volatile int finish = 0;

void sig_handler(int sig)
{
	if (sig == SIGTERM || sig == SIGINT)
		finish = 1;
}

static void msg_arrived(const void *msg, int len)
{
	printf("Message arrived: %s\n", (const char *)msg);
}

int main(int argc, char *argv[])
{
	int retv = 0, llen;
	char *ln;
	struct sigaction sact;
	struct send_queue *sq;


	openlog("cpgmsg", LOG_PERROR, LOG_DAEMON);

	sq = squeue_init(8, "BlockChain", msg_arrived);
	if (!check_pointer(sq, LOG_ERR, nomem))
		return 1;

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
		llen = strlen(ln) + 1;
		retv = squeue_send(sq, ln, llen);
		if (!retv)
			logmsg(LOG_ERR, "Send failed!\n");
		free(ln);
	} while (finish == 0);

	squeue_exit(sq);
	closelog();
	return retv;
}
