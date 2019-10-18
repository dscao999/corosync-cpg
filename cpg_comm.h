#ifndef CPG_COMM_DSCAO__
#define CPG_COMM_DSCAO__
#include <pthread.h>
#include <corosync/cpg.h>

struct cpg_comm {
	cpg_handle_t hand;
	pthread_t thid;
	struct iovec iovec;
	int pin[2];
	uint32_t nodeid;
	volatile int exflag;
	char group[32];
};

struct cpg_comm *cpgcomm_init(const char *gname);
void cpgcomm_exit(struct cpg_comm *cpg);

int cpgcomm_read(struct cpg_comm *cpg, uint32_t *nodeid, void *buf, int buflen);
void cpgcomm_write(struct cpg_comm *cpg, const void *msg, int msglen);
#endif  /* CPG_COMM_DSCAO__ */
