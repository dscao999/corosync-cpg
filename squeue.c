#include <string.h>
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
		sq->ripe = ripemd160_init();
		if (!sq->ripe) {
			free(sq);
			sq = NULL;
		}
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
	ripemd160_exit(sq->ripe);
	free(sq);
}

static inline int squeue_next_slot(const struct send_queue *sq)
{
	return (sq->head + 1) & sq->mask;
}

struct send_entry *squeue_next_entry(struct send_queue *sq,
		const void *msg, int msglen)
{
	int nxt = squeue_next_slot(sq);
	struct send_entry *entry;

	if (nxt == sq->tail)
		return NULL;

	entry = sq->entry_queue + sq->head;
	entry->len = msglen + sizeof(struct mesg_head);
	if (!entry->msgbuf || entry->len > entry->buflen) {
		entry->msgbuf = realloc(entry->msgbuf, entry->len);
		entry->buflen = entry->len;
	}
	if (!entry->msgbuf)
		return NULL;
	memcpy(entry->msgbuf->msg, msg, msglen);
	ripemd160_dgst(sq->ripe, entry->msgbuf->msg, msglen);
	memcpy(entry->msgbuf->ripemd, sq->ripe->H, RIPEMD_LEN);
	entry->msgbuf->confirm = 0;
	sq->head = nxt;
	ripemd160_reset(sq->ripe);
	return entry;
}

void squeue_tail_advance(struct send_queue *sq)
{
	struct send_entry *entry;
	int t = sq->tail;

	while (t != sq->head) {
		entry = sq->entry_queue + t;
		if (entry->vote != 0)
			break;
		t = (t + 1) & sq->mask;
	}
	sq->tail = t;
}

void squeue_confirm(struct send_queue *sq, const struct mesg_head *msghd)
{
	int t = sq->tail;
	struct send_entry *entry;

	while (t != sq->head) {
		entry = sq->entry_queue + t;
		if (memcmp(entry->msgbuf->ripemd, msghd->ripemd, 20) == 0) {
			t = (t + 1) & sq->mask;
			if (entry->vote)
				entry->vote--;
		} else
			break;
	}
}
