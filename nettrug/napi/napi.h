#ifndef BSTGW_NAPI_H
#define BSTGW_NAPI_H

#include <kernel/spinlock.h>
#include <kernel/mutex.h>

//#include <stdatomic.h> // WARNING  / TODO: collides with NPF/thmap's memory_order_* macros!

#include <sys/queue.h>

#include <tee_api_types.h>

struct ifnet; //#include <trust_router.h>
struct bstgw_ether_buffer;

TEE_Result napi_init(void);

struct napi_struct;
SLIST_HEAD(napi_queue, napi_struct);
#define NAPI_QUEUE_INITIALIZER { .slh_first = NULL }

struct napi_dev_list {
	unsigned int nq_slock;
	struct napi_queue nq;
};
extern struct napi_dev_list napi_dlst;

enum gro_result {
	GRO_MERGED,
	GRO_MERGED_FREE,
	GRO_HELD,
	GRO_NORMAL,
	GRO_DROP,
	GRO_CONSUMED,
};
typedef enum gro_result gro_result_t;


struct sk_buff;

gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb);

struct napi_struct {
	/* netif_napi_add */
    int (*poll)(struct napi_struct *, int);
	int weight;
	struct ifnet *net_dev;
	unsigned int idx;

	//gro_result_t (*worker_func)(void *, struct sk_buff *);
    //void *worker_data;

	/* napi_instance_setup */
	unsigned long state;
	unsigned int slock;

	bool is_waiting; // protected by wait_mtx
    struct mutex *wait_mtx;
	bool do_notify; // protected by slock
    struct condvar *wait_cv;

    SLIST_ENTRY(napi_struct) link;
};

// Warning: different from Linux's NAPI state semantics
enum {
	NAPI_STATE_SCHED,	/* Poll worker is running/will run (as soon as the
						   worker thread becomes active) */
	NAPI_STATE_MISSED,	/* Pending poll re-schedule */
	NAPI_STATE_DISABLE,	/* Poll currently (getting) disabled */

	NAPI_STATE_W_ACTIVE, /* bstgw: worker is active, i.e., a NW thread is
							driving the worker_run() function */
};

void napi_instance_setup(struct napi_struct *n, struct condvar *wait_cv, struct mutex *wait_mtx);

/**
 *	netif_napi_add - initialize a NAPI context
 *	@dev:  network device
 *	@napi: NAPI context
 *	@poll: polling function
 *	@weight: default weight
 *
 * netif_napi_add() must be used to initialize a NAPI context prior to calling
 * *any* of the other NAPI-related functions.
 */
// todo: struct net_device?
//void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
//		    int (*poll)(struct napi_struct *, int), int weight);
void netif_napi_add(struct ifnet *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int), int weight);

/**
 *  netif_napi_del - remove a NAPI context
 *  @napi: NAPI context
 *
 *  netif_napi_del() removes a NAPI context from the network device NAPI list
 */
void netif_napi_del(struct napi_struct *napi);

void napi_disable(struct napi_struct *n);
void napi_enable(struct napi_struct *n);

bool napi_schedule_prep_locked(struct napi_struct *n);
void __napi_schedule_locked(struct napi_struct *n);

/**
 *	napi_schedule - schedule NAPI poll
 *	@n: NAPI context
 *
 * Schedule NAPI poll routine to be called if it is not already
 * running.
 */
static inline void napi_schedule(struct napi_struct *n)
{
	uint32_t exceptions = cpu_spin_lock_xsave(&n->slock);
	if (napi_schedule_prep_locked(n))
		__napi_schedule_locked(n);
	cpu_spin_unlock_xrestore(&n->slock, exceptions);
}

bool napi_complete_done(struct napi_struct *n, int work_done);
/**
 *	napi_complete - NAPI processing complete
 *	@n: NAPI context
 *
 * Mark NAPI processing as complete.
 * Consider using napi_complete_done() instead.
 * Return false if device should avoid rearming interrupts.
 */
static inline bool napi_complete(struct napi_struct *n)
{
	return napi_complete_done(n, 0);
}

/* Generic rx_handler for pushing packets from a NIC device to the
 * trusted router */
int bstgw_generic_rx_handler(struct ifnet *net_dev,
    struct bstgw_ether_buffer **pkts /* *pkts[] */, unsigned int pktcount);

#endif /* BSTGW_NAPI_H */
