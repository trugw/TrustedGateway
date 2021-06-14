#include <stdio.h>
#include <stdint.h>

#include "npf_router.h"
#include "utils.h"

//#define	RX_RING_SIZE	1024
//#define	TX_RING_SIZE	1024

//		.max_rx_pkt_len = ETHER_MAX_LEN,


int
ifnet_setup(npf_router_t *router __unused, ifnet_t *ifp __unused)
{
	/*
	 * TODO: Start the Ethernet port and enable the promiscuous mode.
	 */
	/*
	if (rte_eth_dev_start(port_id) < 0) {
		return -1;
	}
	rte_eth_promiscuous_enable(port_id);
	*/
	return 0;
}

int
ifnet_ifattach(npf_router_t *router, ifnet_t *ifp)
{
	/* Setup Router/Netif part */
	ifp->arp_cache = thmap_create(0, NULL, THMAP_NOCOPY);
	if (!ifp->arp_cache) return -1;

	uint32_t exceptions;

	exceptions = cpu_spin_lock_xsave(&ifp->egress_queue->slock);
	ifp->egress_queue->pkt = malloc(sizeof(bstgw_ethbuf_t *) * trusted_router->pktqueue_size);
	if (!ifp->egress_queue->pkt) {
		cpu_spin_unlock_xrestore( &ifp->egress_queue->slock, exceptions );
		thmap_destroy(ifp->arp_cache);
		return -1;
	}
	ifp->egress_queue->capacity = router->pktqueue_size;
	cpu_spin_unlock_xrestore( &ifp->egress_queue->slock, exceptions );

    bstgw_ethpool_increase(NULL, ifp->egress_queue->capacity);


	npfk_ifmap_attach(router->npf, ifp);
	return 0;
}

void
ifnet_ifdetach(npf_router_t *router, ifnet_t *ifp)
{
	npfk_ifmap_detach(router->npf, ifp);

	// TODO: free pointers
//	free(ifp->egress_queue);

	uint32_t exceptions;
	exceptions = cpu_spin_lock_xsave(&ifp->egress_queue->slock);

	// drop enqueued packets, then free the packet list
	bstgw_pktq_locked_drop_bufs(ifp->egress_queue, ifp->egress_queue->count);
	ifp->egress_queue->capacity = 0;
	free(ifp->egress_queue->pkt);

	cpu_spin_unlock_xrestore( &ifp->egress_queue->slock, exceptions );

	thmap_destroy(ifp->arp_cache);
}

/*
void
ifnet_put(ifnet_t *ifp)
{
	(void)ifp;
	// release
}
*/
