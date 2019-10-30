#ifndef SND_QUEUE_DSCAO__
#define SND_QUEUE_DSCAO__
#include <stdlib.h>
#include "ecc256/ripemd160.h"

struct mesg_head {
	unsigned char ripemd[RIPEMD_LEN];
	unsigned char confirm;
	unsigned char msg[0];
};
struct send_entry {
	struct mesg_head *msgbuf;
	int len, buflen;
	unsigned char vote;
};

struct send_queue {
	struct ripemd160 *ripe;
	unsigned short mask;
	unsigned short head;
	unsigned short tail;
	struct send_entry entry_queue[0];
};

struct send_queue *squeue_init(int len);
void squeue_exit(struct send_queue *sq);

struct send_entry *squeue_next_entry(struct send_queue *sq,
		const void *msg, int len);
void squeue_tail_advance(struct send_queue *sq);

void squeue_confirm(struct send_queue *sq, const struct mesg_head *msghd);

#endif  /* SND_QUEUE_DSCAO__ */
