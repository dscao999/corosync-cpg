#ifndef SND_QUEUE_DSCAO__
#define SND_QUEUE_DSCAO__
#include <stdlib.h>
#include "ecc256/ripemd160.h"
#include "cpg_comm.h"

struct mesg_head {
	unsigned char ripemd[RIPEMD_LEN];
	unsigned char confirm;
	unsigned char msg[0];
};
struct send_entry {
	struct mesg_head *msgbuf;
	int len, buflen;
	unsigned int ack;
	unsigned int exp;
};

struct send_queue {
	struct ripemd160 *ripe;
	struct cpg_comm *cpg;
	void (*rcvmsg)(const void *msg, int len);
	unsigned short mask;
	unsigned short head;
	unsigned short tail;
	struct send_entry entry_queue[0];
};

struct send_queue *squeue_init(int noentries, const char *group,
		void (*rcvmsg)(const void *msg, int len));
void squeue_exit(struct send_queue *sq);

int squeue_send(struct send_queue *sq,
		const void *msg, int len);

#endif  /* SND_QUEUE_DSCAO__ */
