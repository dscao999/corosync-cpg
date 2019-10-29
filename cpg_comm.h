#ifndef CPG_COMM_DSCAO__
#define CPG_COMM_DSCAO__
#include <pthread.h>
#include <corosync/cpg.h>

#define CPG_CHUNK_SIZE	3145728
#define MAX_NUM_NODES	16

struct cpg_comm {
	void (*rcvmsg)(struct cpg_comm *cpg, uint32_t node, const void *msg, size_t len);
	cpg_handle_t hand;
	pthread_t thid;
	struct iovec iovec;
	uint32_t nodeid;
	volatile int exflag;
	char group[32];
	unsigned char nodon[MAX_NUM_NODES];
	unsigned char ripemd[20];
	unsigned char confirm;
};

static inline int cpg_numnodes(const struct cpg_comm *cpg)
{
	int sum = 0, i;

	for (i = 0; i < MAX_NUM_NODES; i++)
		sum += cpg->nodon[i];
	return sum;
}

struct cpg_comm *cpgcomm_init(const char *gname,
		void (*revmsg)(struct cpg_comm *cpg, uint32_t node, const void *msg, size_t len));
void cpgcomm_exit(struct cpg_comm *cpg);

void cpgcomm_write(struct cpg_comm *cpg, void *msg, int msglen);
#endif  /* CPG_COMM_DSCAO__ */
