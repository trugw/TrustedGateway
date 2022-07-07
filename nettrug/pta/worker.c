/*
 * Copyright 2022 Fabian Schwarz and CISPA Helmholtz Center for Information Security
 * All rights reserved.
 *
 * Use is subject to the license terms, as specified in the project LICENSE file.
 *
 * ----------------------------------------------------------------------
 *
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the NPF_LICENSE file.
 */

/*
 * Worker of the NPF-router.
 */

#include <stdio.h>

#include "npf_router.h"
#include "utils.h"

#include <newlib/netinet/ip.h>

#include "bstgw_netif_utils.h"
#include <drivers/bstgw/imx_fec.h>

//#define BSTGW_WORKER_DEBUG

static int
firewall_process(npf_t *npf, bstgw_ethbuf_t **mp, ifnet_t *ifp, const int di)
{
	int error;

	error = npfk_packet_handler(npf, (struct mbuf **)mp, ifp, di);

	/* Note: NPF may consume the packet. */
	if (error || *mp == NULL) {
		if (*mp) {
			bstgw_ethpool_buf_free(*mp);
		} else {
		}
		return -1;
	}

	return 0;
}

static int
router_if_output(worker_t *worker, ifnet_t *out_ifp, bstgw_ethbuf_t *m)
{
	assert(worker && out_ifp && m);

	const npf_mbuf_priv_t *minfo = bstgw_ethbuf_priv(m);
	assert(minfo);
	//const npf_mbuf_priv_t *minfo = rte_mbuf_to_priv(m);
	struct rte_ether_hdr *eh;

	/*
	 * If the packet already has L2 header, then nothing more to do.
	 */
	if ((minfo->flags & MBUF_NPF_NEED_L2) == 0) {
		return 0;
	}

	/*
	 * Add the Ethernet header.
	 */
	ASSERT(m->l2_len == RTE_ETHER_HDR_LEN);
	eh = (void *)bstgw_ethbuf_prepend(m, RTE_ETHER_HDR_LEN);
	//eh = (void *)rte_pktmbuf_prepend(m, RTE_ETHER_HDR_LEN);
	if (eh == NULL) {
		EMSG("[%s->%s] failed to prepend ethernet header",
			worker->net_if->name, out_ifp->name);
		return -1; // drop
	}
	rte_ether_addr_copy(&out_ifp->hwaddr, &eh->s_addr);

	/*
	 * Perform ARP resolution.
     *
     * TODO: If output netif is VNIC, use the MAC exposed by it to the
     *       untrusted world directly as dstMAC -- no need for ARP resolution.
	 */
	if (arp_resolve(worker, &minfo->route, &eh->d_addr) == -1) {
		return -1; // drop
	}
	eh->ether_type = minfo->ether_type;

	return 0;
}


int
pktq_prepare_enqueue(worker_t *worker, unsigned out_if_idx, bstgw_ethbuf_t *m) {


	ifnet_t *out_ifp = ifnet_getbyidx(out_if_idx);
	if (__predict_false(!out_ifp)) {
		EMSG("Unknown output interface/netdev! (idx: %u)", out_if_idx);
		return -1; // drop
	}


	if (router_if_output(worker, out_ifp, m) == -1) {
		return -1; // drop
	}

	npf_mbuf_priv_t *minfo = bstgw_ethbuf_priv(m);
	minfo->out_if_idx = out_if_idx;
	return 0;
}


/*
 * pkt_enqueue: enqueue the packet for TX.
 * 	(legacy API, todo: remove)
 */
inline int
pktq_enqueue(worker_t *worker, unsigned out_if_idx, bstgw_ethbuf_t *m) {
	int ret = pktq_prepare_enqueue(worker, out_if_idx, m);
	if(ret) return ret;
	ret = pktq_do_menqueue(worker, out_if_idx, &m, 1);
	return ret>=0 ? 0 : ret; // forward errors
}

/*
 * pkt_m_enqueue: enqueue matching (egr_idx) packets from the pool for TX.
 */
int
pktq_do_menqueue(worker_t *worker, unsigned out_if_idx, bstgw_ethbuf_t **m_pool, unsigned int npkts)
{
	if(__predict_false( !m_pool || !npkts )) return 0;

	ifnet_t *out_ifp = ifnet_getbyidx(out_if_idx);
	if (__predict_false(!out_ifp)) {
		EMSG("Unknown output interface/netdev! (idx: %u)", out_if_idx);
		return -1; // drop
	}

	pktqueue_t *pq = out_ifp->egress_queue;
	uint32_t exceptions, negr_pkts = 0;

	exceptions = cpu_spin_lock_xsave(&pq->slock);
	negr_pkts = bstgw_pktq_locked_bulk_add_buff(pq, m_pool, npkts);
	cpu_spin_unlock_xrestore(&pq->slock, exceptions);

	// free those that didn't fit
	for(int i=negr_pkts; i<npkts; i++)
		bstgw_ethpool_buf_free(m_pool[i]);

#ifdef BSTGW_WORKER_DEBUG
		if (negr_pkts < npkts)
			EMSG("[%s] queue of %s is full [late out]",
				worker->net_if->name, out_ifp->name);
#endif

	if(!negr_pkts) return 0;

#ifdef BSTGW_WORKER_DEBUG
	DMSG("[%s] %u new packet enqueued", worker->net_if->name, negr_pkts);
#endif

	// do we need to notify?
	if (worker->net_if->ifnet_idx != out_if_idx) {
#ifdef BSTGW_WORKER_DEBUG
		DMSG("[%s] Schedule net-dev %s", worker->net_if->name, out_ifp->name);
#endif
		out_ifp->egress_ntfy_handler(out_ifp);
	} else {
#ifdef BSTGW_WORKER_DEBUG
		DMSG("[%s] Output == Input interface, so no need to notify other worker",
			worker->net_if->name);
#endif
	}

	return negr_pkts;
}

int
router_pktq_tx(ifnet_t *ifp)
{
	assert(ifp && ifp->egress_queue && ifp->xmit_handler);

	pktqueue_t *pq = ifp->egress_queue;
	uint32_t exceptions;

	// new: tx queue not yet started / temporarily stopped
	// FIXME: only correctly used by FEC atm
	if(ifp->trust_status == BSTGW_IF_EXTERNAL) {
		if(__predict_false( netif_tx_queue_stopped(pq) )) {
			return 0;
		}
	}

	exceptions = cpu_spin_lock_xsave(&pq->slock);


#ifdef BSTGW_WORKER_DEBUG
	unsigned __total = pq->count;
#endif

	// grab contiguous packets
	unsigned pktburst = bstgw_pktq_locked_contig_bufs(pq);
	cpu_spin_unlock_xrestore( &pq->slock, exceptions );

	if (!pktburst) return 0;

#ifdef BSTGW_WORKER_DEBUG
	DMSG("[%s] Going to call xmit_handler() with pktburst:=%u (of %u)",
		ifp->name, pktburst, __total);
#endif
	int sent = ifp->xmit_handler(ifp, &pq->pkt[pq->next_read], pktburst);

#ifdef BSTGW_WORKER_DEBUG
	DMSG("[%s] Returned from xmit_handler() with ret: %d",
		ifp->name, sent);
#endif
	if (!sent) return sent;
	if (sent < 0) {
		return sent;
	}

	// Note: sent packets are freed by the xmit_handler()
	exceptions = cpu_spin_lock_xsave(&pq->slock);

	// mark queue spots as free
	if(__predict_false( !bstgw_pktq_locked_drop_bufs(pq, sent) )) {
		cpu_spin_unlock_xrestore(&pq->slock, exceptions);
		panic("BUG: xmit_handler() packet sent is > packet queue capacity");
	}
	cpu_spin_unlock_xrestore(&pq->slock, exceptions);

	// TODO: could directly loop if pktburst was < pq->count

	return sent;
}

static int
l2_input(worker_t *worker, bstgw_ethbuf_t *m)
{

	npf_mbuf_priv_t *minfo = bstgw_ethbuf_priv(m);  //rte_mbuf_to_priv(m);
	const struct rte_ether_hdr *eh;

	/*
	 * Do we have an L2 header?  If not, then nothing to do.
	 */
	// NOTE: this should not happen!
	if(__predict_false( (m->packet_type & RTE_PTYPE_L2_MASK) == 0 ))
		panic("l2_input: no L2 header");

	/*
	 * We have an L2 header, which must be Ethernet.  Save it in the
	 * mbuf private area for later pre-pending.
	 */

	eh = (const struct rte_ether_hdr *)bstgw_ethbuf_data_ptr(m, 0); //rte_pktmbuf_mtod(m, const struct rte_ether_hdr *);
	minfo->ether_type = eh->ether_type;

	ASSERT(sizeof(struct rte_ether_hdr) == RTE_ETHER_HDR_LEN);
	m->l2_len = RTE_ETHER_HDR_LEN;

	//bstgw_ethbuf_dump_data(m);

	switch (ntohs(eh->ether_type)) {
	case RTE_ETHER_TYPE_ARP:
		return arp_input(worker, m);
	// TODO: is that correct?!
	case RTE_ETHER_TYPE_IPV4: {
		m->packet_type |= RTE_PTYPE_L3_IPV4;
		break;
		}
	case RTE_ETHER_TYPE_IPV6: {
//		m->packet_type |= RTE_PTYPE_L3_IPV6;
		bstgw_ethpool_buf_free(m); // drop
		return -1;
		}
	case 0x88e1:
	case 0x8912:
		/* fall-through */
	default: {
		bstgw_ethpool_buf_free(m); // drop
		return -1;
		}
	}

	/*
	 * Remove the L2 header as we are preparing for L3 processing.
	 */
	// cuts size from front (ideal for header cutting)
	//rte_pktmbuf_adj(m, sizeof(struct rte_ether_hdr));
	if(__predict_false( !bstgw_ethbuf_precut(m, sizeof(struct rte_ether_hdr)) ))
		panic("ethernet header precutting failed!");

	minfo->flags |= MBUF_NPF_NEED_L2;
	return 0;
}

/*
 * ip_route: find a route for the IPv4/IPv6 packet.
 */
static int
router_ip_route(npf_router_t *router, bstgw_ethbuf_t *m,
	bool untrusted_source __maybe_unused)
{
	npf_mbuf_priv_t *minfo = bstgw_ethbuf_priv(m);
	//npf_mbuf_priv_t *minfo = rte_mbuf_to_priv(m);
	route_info_t *rt = &minfo->route;
	const void *addr;
	unsigned alen;
#ifdef BSTGW_NON_NPF_ANTI_IP_SPOOFING
	const void *src_addr;
	unsigned src_alen;
#endif

	/*
	 * Determine whether it is IPv4 or IPv6 packet.
	 */
	if (RTE_ETH_IS_IPV4_HDR(m->packet_type)) {
		const struct ip *ip4;

		ip4 = (const struct ip *)bstgw_ethbuf_data_ptr(m, 0);
		//ip4 = rte_pktmbuf_mtod(m, const struct ipv4_hdr *);
		addr = &ip4->ip_dst;
		alen = sizeof(ip4->ip_dst);
		// TODO: TX_IPCKSUM means "offload tx checksum to HW"
		// TODO: TX_IPV4 means "Packet is IPv4. This flag must be set when using
		//		any offload feature (...) to tell the NIC
		//TODO: m->ol_flags |= (PKT_TX_IPV4 | PKT_TX_IP_CKSUM);

#ifdef BSTGW_NON_NPF_ANTI_IP_SPOOFING
		/* Source IP must be one of the non-virtual Router IPs */
		if (untrusted_source) {
			src_addr = &ip4->ip_src;
			src_alen = sizeof(ip4->ip_src);

			// FIXME:  optimize performance of the implementation
			bool valid = false;
			for(int j=0; j < router->ext_ndevs_count; j++) {
				ifnet_t *ext_ndev = router->ext_ndevs[j];
				for(int i=0; i < ext_ndev->ips_cnt; i++) {
					if (memcmp(&ext_ndev->if_ips[i], src_addr, src_alen) == 0) {
						valid = true;
						goto found_src_ip;
					}
				}
			}
found_src_ip:
			if (!valid) {
				EMSG("IPv4 source spoofing attempt. Dropping the packet.");
				return -1;
			}
		}
#endif

		/* to check if as expected? */

		if (ip4->ip_ttl <= 1) {
			EMSG("ipv4 packet: TTL <= 1");
			/* ICMP_TIMXCEED */
			return -1;
		}
		/* TODO: ip4->time_to_live--; */

	// TODO: we do not support IPv6 at the moment
	} else if (RTE_ETH_IS_IPV6_HDR(m->packet_type)) {
		IMSG("IPV6 is not supported yet");
		return -1;

	} else {
		EMSG("Unknown L3 protocol");
		return -1;
	}

	/*
	 * Lookup the route and get the interface and next hop.
	 */
	if (route_lookup(router->rtable, addr, alen, rt) == -1) {
		return -1;
	}
	if (!rt->addr_len) {
		/*
		 * There is next-hop specified with route, so it will be
		 * the destination address.
		 */
		memcpy(&rt->next_hop, addr, alen);
		rt->addr_len = alen;
	}
	return rt->if_idx;
}

static int
router_ip_output(worker_t *worker, bstgw_ethbuf_t *m, const unsigned out_if_idx)
{
	
	ifnet_t *ifp;
	int ret;

	/*
	 * Firewall -- outbound.
	 */
	if ((ifp = ifnet_getbyidx(out_if_idx)) == NULL) {
		bstgw_ethpool_buf_free(m);
		return -1;
	}

	// TODO: move out of hot-path
	ret = firewall_process(worker->npf, &m, ifp, PFIL_OUT);
	if (ret) {
		/* Consumed or dropped. */
		return -1;
	}

	/*
	 * Prepare enqueue for the destination interface.
	 */
	if (pktq_prepare_enqueue(worker, out_if_idx, m) == -1) {
		bstgw_ethpool_buf_free(m);
		return -1;
	}

	return 0;
}

void
router_if_input(worker_t *worker, bstgw_ethbuf_t **pkts, unsigned int npkts)
{
	assert(worker && pkts && worker->net_if);
	if(__predict_false( !npkts )) return;

	unsigned int dev_count = ifnet_dev_count();

	bstgw_ethbuf_t **out_arr = malloc(sizeof(bstgw_ethbuf_t *) * npkts * dev_count);
	bstgw_ethbuf_t ***ptr_arr = malloc(sizeof(bstgw_ethbuf_t **) * dev_count);
	if (!out_arr || !ptr_arr) {
		EMSG("OOM inside router_if_input()");
		for(int i=0; i<npkts; i++) bstgw_ethpool_buf_free(pkts[i]);
		if(out_arr) free(out_arr);
		return;
	}
	for (int i=0; i<dev_count; i++) ptr_arr[i] = &out_arr[i * npkts];


	/*
	 * Route each packet.
	 */
	bool is_untrusted_src = (worker->net_if->trust_status == BSTGW_IF_NONSEC);
	uint8_t egr_idx = 0;
	for (unsigned i = 0; i < npkts; i++) {
		bstgw_ethbuf_t *m = pkts[i];
		int out_if_idx;

		/*
		 * L2 processing.
		 */
		if (l2_input(worker, m)) {
			/* Consumed (dropped or re-enqueued). */
			pkts[i] = NULL;
			continue;
		}

		/*
		 * Firewall -- inbound.
		 */
		if (firewall_process(worker->npf, &m, worker->net_if, PFIL_IN)) {
			/* Consumed or dropped. */
			pkts[i] = NULL;
			continue;
		}

		/*
		 * L3 routing.
		 */
		if (__predict_false( ( out_if_idx = router_ip_route(worker->router, m,
			is_untrusted_src) ) == -1 ))
		{
			/* Packet could not be routed -- drop it. */
			bstgw_ethpool_buf_free(m);
			pkts[i] = NULL;
			continue;
		}
		//(void)router_ip_output(worker, m, out_if_idx);
		if(router_ip_output(worker, m, out_if_idx) < 0) {
			pkts[i] = NULL;
			continue;
		}
		egr_idx |= (1 << out_if_idx);

		*ptr_arr[out_if_idx-1] = m;
		*ptr_arr[out_if_idx-1]++;
	}

	/*
	 * Batch egress enqueue + notify
	 */
	for (unsigned j = 1; j <= dev_count; j++) {
		if (! (egr_idx & (1 << j)) ) continue;
		// fixme: if this pointer semantic is undef. behaviour, use offsets/cntr
		pktq_do_menqueue(worker, j, &out_arr[(j-1) * npkts],
			(unsigned int)( ptr_arr[j-1] - &out_arr[(j-1) * npkts] ));
	}

	free(ptr_arr);
	free(out_arr);

	/*
	 * Transmit packets of the worker interface.
	 */
	//if (worker->net_if->xmit_handler && worker->net_if->direct_xmit) {
	//	router_pktq_tx(worker->net_if);
	//}
}
