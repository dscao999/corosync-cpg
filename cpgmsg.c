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

#define unlikely(x)	__builtin_expect(x, 0)

static volatile int finish = 0;
static const char *file_b_mark = "File Follows: ";
static const char *file_e_mark = "xFile End!x";

void sig_handler(int sig)
{
	if (sig == SIGTERM || sig == SIGINT)
		finish = 1;
}

static void save_file(struct cpg_comm *cpg, char *buf)
{
	char *fname, file[64];
	int num, numw;
	FILE *fo;
	uint32_t nodeid;

	if (strncmp(buf, file_b_mark, strlen(file_b_mark)) != 0)
		return;
	fname = strrchr(buf, '/');
	if (fname == buf)
		fname = strrchr(buf, ' ');
	fname += 1;
	strcpy(file, "/tmp/");
	strcat(file, fname);
	fo = fopen(file, "wb");
	if (!fo) {
		logmsg(LOG_ERR, "Cannot open file %s for writing: %d\n",
				file, errno);
		return;
	}
	logmsg(LOG_INFO, "Writing file: %s\n", file);

	do {
		num = cpgcomm_read(cpg, &nodeid, buf, CPG_CHUNK_SIZE);
		if (num == 0)
			continue;
		if (num > CPG_CHUNK_SIZE)
			logmsg(LOG_ERR, "Message overflow! %d\n", num);
		if (strncmp(buf, file_e_mark, strlen(file_e_mark)) == 0)
			break;
		numw = fwrite(buf, 1, num, fo);
		assert(numw == num);
	} while (1);
	fclose(fo);
	logmsg(LOG_INFO, "Finished Receiving: %s\n", file);
}

static void *watch_mesg(void *arg)
{
	struct cpg_comm *cpg = arg;
	char *msgbuf;
	int num;
	uint32_t nodeid;

	msgbuf = malloc(CPG_CHUNK_SIZE+1);
	do {
		num = cpgcomm_read(cpg, &nodeid, msgbuf, CPG_CHUNK_SIZE);
		if (num > CPG_CHUNK_SIZE)
			logmsg(LOG_ERR, "Message overflow! %d\n", num);
		else if (num > 0) {
			msgbuf[num] = 0;
			printf("Node: %d->%s\n", (int)nodeid, msgbuf);
			save_file(cpg, msgbuf);
		}
	} while (cpg->exflag == 0);
	free(msgbuf);
	return NULL;
}

static int send_file(const char *fname, struct cpg_comm *cpg)
{
	FILE *fin;
	int nmb;
	unsigned int len;
	char *buf;

	len = 0;
	buf = malloc(CPG_CHUNK_SIZE);
	if (!buf) {
		logmsg(LOG_CRIT, "Out of Memory!\n");
		return 0;
	}
	fin = fopen(fname, "rb");
	if (!fin) {
		logmsg(LOG_WARNING, "Cannot open file: %s->%d\n",
				fname, errno);
		free(buf);
		return 0;
	}
	do {
		nmb = fread(buf, 1, CPG_CHUNK_SIZE, fin);
		if (unlikely(ferror(fin)))
			logmsg(LOG_ERR, "fread error: %d\n", errno);
		if (nmb > 0)
			cpgcomm_write(cpg, buf, nmb);
		len += nmb;
	} while (!feof(fin));
	fclose(fin);
	free(buf);

	return len;
}

int main(int argc, char *argv[])
{
	struct cpg_comm *cpg;
	pthread_t thid;
	int retv, sysret;
	char *ln, fcmd[128];
	struct sigaction sact;
	struct stat fst;

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
		cpgcomm_write(cpg, ln, strlen(ln));
		sysret = stat(ln, &fst);
		if (sysret == 0 && S_ISREG(fst.st_mode) &&
				(fst.st_mode & S_IROTH)) {
			strcpy(fcmd, file_b_mark);
			strcat(fcmd, ln);
			cpgcomm_write(cpg, fcmd, strlen(fcmd));
			send_file(ln, cpg);
			strcpy(fcmd, file_e_mark);
			cpgcomm_write(cpg, fcmd, strlen(fcmd));
		}
		free(ln);
	} while (finish == 0);

	cpg->exflag = 1;
	pthread_join(thid, NULL);

exit_10:
	cpgcomm_exit(cpg);
	closelog();
	return retv;
}
