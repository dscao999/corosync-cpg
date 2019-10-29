#ifndef CPG_COMM_DSCAO__
#define CPG_COMM_DSCAO__
#include <pthread.h>
#include <corosync/cpg.h>

#define CPG_CHUNK_SIZE	3145728

struct cpg_comm {
	void (*rcvmsg)(uint32_t node, const void *msg, size_t len);
	cpg_handle_t hand;
	pthread_t thid;
	struct iovec iovec;
	uint32_t nodeid;
	volatile int exflag, dispatching;
	char group[32];
};

struct cpg_comm *cpgcomm_init(const char *gname,
		void (*revmsg)(uint32_t node, const void *msg, size_t len));
void cpgcomm_exit(struct cpg_comm *cpg);

void cpgcomm_write(struct cpg_comm *cpg, void *msg, int msglen);
#endif  /* CPG_COMM_DSCAO__ */
