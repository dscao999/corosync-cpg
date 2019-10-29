#ifndef SND_QUEUE_DSCAO__
#define SND_QUEUE_DSCAO__
#include "squeue.h"

struct send_queue *squeue_init(int len)
{
	int cnt = 0, bits = len;
	int mask, mlen;
	struct send_queue *sq;

	while (bits != 0) {
		cnt++;
		bits >>= 1;
	}
	mask = (1 << cnt) - 1;
	mlen = sizeof(struct send_queue) +
		(mask + 1)*sizeof(struct send_entry);
	sq = malloc(mlen);
	if (sq) {
		memset(sq, 0, mlen);
		sq->mask = mask;
		
	}
	return sq;
}

void squeue_exit(struct send_queue *sq)
{
	int i;
	struct send_entry *entry;

	for (i = 0; i <= sq->mask; i++) {
		entry = sq->entry_queue + i;
		if (entry->msgbuf)
			free(entry->msgbuf);
	}
	free(sq);
}

static inline int squeue_next_slot(const struct send_queue *sq)
{
	return (sq->head + 1) & sq->mask;
}

struct send_entry *squeue_next_entry(struct send_queue *sq)
{
	int nxt = squeue_next_slot(sq);
	struct send_entry *entry;

	if (nxt == tail)
		return NULL;
	entry = sq->entry_queue + sq->head;
	sq->head = nxt;
	return entry;
}

void squeue_tail_advance(struct send_queue *sq)
{
}

#endif  /* SND_QUEUE_DSCAO__ */
