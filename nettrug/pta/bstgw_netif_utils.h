#ifndef BSTGW_NETIF_UTILS_H
#define BSTGW_NETIF_UTILS_H

#include <bstgw_pktq.h>
#include <npf_router.h>

#include <napi.h>

#include <kernel/spinlock.h>
#include <assert.h>

static inline pktqueue_t *
netdev_get_tx_queue(ifnet_t *ndev, unsigned int idx __unused) {
	assert(ndev);
	return ndev->egress_queue;
}

static inline void netif_tx_start_queue(pktqueue_t *pq) {
	assert(pq);
	atomic_store_explicit(&pq->stopped, false, memory_order_relaxed);
}

static inline void netif_tx_start_all_queues(ifnet_t *ndev) {
	assert(ndev && ndev->egress_queue);
	netif_tx_start_queue(ndev->egress_queue);
}

static inline void netif_tx_stop_queue(pktqueue_t *pq) {
	assert(pq);
	atomic_store_explicit(&pq->stopped, true, memory_order_relaxed);
}

static inline void netif_tx_disable(ifnet_t *ndev) {
	assert(ndev);
	netif_tx_stop_queue(ndev->egress_queue);
}

static inline bool netif_tx_queue_stopped(pktqueue_t *pq) {
	assert(pq);
	return atomic_load_explicit(&pq->stopped, memory_order_relaxed);
}

//static inline void netif_tx_wake_queue(pktqueue_t *pq) {
static inline void
netif_tx_wake_queue(ifnet_t *ndev, unsigned int idx __unused) {
	assert(ndev && ndev->egress_queue);
	bool was_stopped, true_val = true;
	pktqueue_t *pq = ndev->egress_queue;

	was_stopped = atomic_compare_exchange_strong_explicit(&pq->stopped,
		&true_val, false, memory_order_relaxed, memory_order_relaxed);

	if (was_stopped) ndev->egress_ntfy_handler(ndev);
}

#endif /* BSTGW_NETIF_UTILS_H */
