#ifndef SND_QUEUE_DSCAO__
#define SND_QUEUE_DSCAO__
#include <stdlib.h>

struct mesg_head {
	unsigned char ripemd[20];
	unsigned char confirm;
	unsigned char msg[0];
};
struct send_entry {
	struct mesg_head *msgbuf;
	int maxlen;
	unsigned char vote;
};

struct send_queue {
	unsigned short mask;
	unsigned short head;
	unsigned short tail;
	struct send_entry entry_queue[0];
};

struct send_queue *squeue_init(int len);
void squeue_exit(struct send_queue *sq);

struct send_entry *squeue_next_entry(struct send_queue *sq);
void squeue_tail_advance(struct send_queue *sq);

#endif  /* SND_QUEUE_DSCAO__ */
