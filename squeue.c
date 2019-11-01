#include <string.h>
#include "loglog.h"
#include "squeue.h"

static void squeue_confirm(struct send_queue *sq,
		const struct mesg_head *msg, int node);
static void squeue_tail_advance(struct send_queue *sq);

static void queue_recv(struct cpg_comm *cpg, uint32_t node,
		const void *msg, size_t len)
{
	struct mesg_head ack;
	const struct mesg_head *msghead = msg;
	int msglen = len - sizeof(struct mesg_head);
	struct send_queue *sq = cpg->privdata;

	if (msghead->confirm == 0) {
		ripemd160_dgst(sq->ripe, msghead->msg, msglen);
		memcpy(ack.ripemd, sq->ripe->H, RIPEMD_LEN);
		if (memcmp(ack.ripemd, msghead->ripemd, RIPEMD_LEN) == 0) {
			ack.confirm = 1;
			cpgcomm_write(cpg, &ack, RIPEMD_LEN+1);
			sq->rcvmsg(msghead->msg, msglen);
		}
		ripemd160_reset(sq->ripe);
	} else {
		squeue_confirm(cpg->privdata, msghead, node);
	}
}

struct send_queue *squeue_init(int noentries, const char *group,
		void (*rcvmsg)(const void *msg, int len))
{
	int cnt = 0, bits = noentries;
	int mask, mlen, rem = 0;
	struct send_queue *sq;
	struct cpg_comm *cpg;

	if (noentries <= 1)
		return NULL;
	while (bits != 0) {
		cnt++;
		if (bits & 1)
			rem++;
		bits >>= 1;
	}
	if (rem == 1)
		cnt--;
	mask = (1 << cnt) - 1;
	mlen = sizeof(struct send_queue) +
		(mask + 1)*sizeof(struct send_entry);
	sq = malloc(mlen);
	if (!check_pointer(sq, LOG_CRIT, nomem))
		return NULL;
	memset(sq, 0, mlen);
	sq->mask = mask;
	sq->rcvmsg = rcvmsg;
	sq->ripe = ripemd160_init();
	if (!check_pointer(sq->ripe, LOG_CRIT, nomem)) {
		free(sq);
		return NULL;
	}
	cpg = cpgcomm_init(group, sq, queue_recv);
	if (!check_pointer(cpg, LOG_CRIT, nomem)) {
		ripemd160_exit(sq->ripe);
		free(sq);
		return NULL;
	}
	sq->cpg = cpg;
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
	cpgcomm_exit(sq->cpg);
	ripemd160_exit(sq->ripe);
	free(sq);
}

static inline int squeue_next_slot(const struct send_queue *sq)
{
	return (sq->head + 1) & sq->mask;
}

int squeue_send(struct send_queue *sq,
		const void *msg, int msglen)
{
	int nxt = squeue_next_slot(sq);
	struct send_entry *entry;

	if (nxt == sq->tail) {
		squeue_tail_advance(sq);
		if (nxt == sq->tail) {
			logmsg(LOG_ERR, "No empty slot in send queue!\n");
			return 0;
		}
	}

	entry = sq->entry_queue + sq->head;
	entry->len = msglen + sizeof(struct mesg_head);
	if (!entry->msgbuf || entry->len > entry->buflen) {
		entry->msgbuf = realloc(entry->msgbuf, entry->len);
		entry->buflen = entry->len;
	}
	if (!check_pointer(entry->msgbuf, LOG_CRIT, nomem))
		return 0;
	memcpy(entry->msgbuf->msg, msg, msglen);
	ripemd160_dgst(sq->ripe, entry->msgbuf->msg, msglen);
	memcpy(entry->msgbuf->ripemd, sq->ripe->H, RIPEMD_LEN);
	entry->msgbuf->confirm = 0;
	entry->ack = (1 << sq->cpg->nodeid);
	entry->exp = sq->cpg->nodon;
	cpgcomm_write(sq->cpg, entry->msgbuf, entry->len);
	sq->head = nxt;
	ripemd160_reset(sq->ripe);
	return 1;
}

static void squeue_tail_advance(struct send_queue *sq)
{
	struct send_entry *entry;
	int t = sq->tail;

	while (t != sq->head) {
		entry = sq->entry_queue + t;
		if (entry->ack < entry->exp) {
			if (entry->exp > sq->cpg->nodon) {
				entry->exp = sq->cpg->nodon;
				continue;
			}
			break;
		}
		t = (t + 1) & sq->mask;
	}
	sq->tail = t;
}

static void squeue_confirm(struct send_queue *sq, const struct mesg_head *msghd, int node)
{
	int t = sq->tail;
	struct send_entry *entry;

	while (t != sq->head) {
		entry = sq->entry_queue + t;
		if (memcmp(entry->msgbuf->ripemd, msghd->ripemd, 20) == 0)
			entry->ack |= (1 << node);
		t = (t + 1) & sq->mask;
	}
}
