/*
 * Copyright 2022 Fabian Schwarz (CISPA Helmholtz Center for Information Security)
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
 * Minimalistic ARP implementation (demo only).
 *
 * Ethernet Address Resolution Protocol, RFC 826, November 1982.
 */

#include <stdlib.h>
#include <newlib/arpa/inet.h>

#include <dpdk/rte_ether.h>
#include <dpdk/rte_arp.h>

#include "bstgw_eth_buf.h"

#include "npf_router.h"
#include "utils.h"

//#define BSTGW_ARP_DEBUG

typedef struct {
	uint32_t		ipaddr;
	struct rte_ether_addr	hwaddr;
} arp_entry_t;

static void
arp_cache(ifnet_t *ifp, const uint32_t *ipaddr,
    const struct rte_ether_addr *hwaddr, const bool allow_new)
{

	arp_entry_t *ac;
	bool ok;

	ac = thmap_get(ifp->arp_cache, ipaddr, sizeof(uint32_t));
	if (__predict_true(ac)) {
		/* TODO: Update. */
		return;
	}
	if (!allow_new || (ac = malloc(sizeof(arp_entry_t))) == NULL) {
		if(allow_new && !ac) EMSG("OOM");
		return;
	}
	memcpy(&ac->ipaddr, ipaddr, sizeof(ac->ipaddr));
	memcpy(&ac->hwaddr, hwaddr, sizeof(ac->hwaddr));

	ok = thmap_put(ifp->arp_cache, &ac->ipaddr, sizeof(ac->ipaddr), ac) == ac;
	if (__predict_false(!ok)) {
		/* Race: already cached. */
		free(ac);
	}
}

static int
arp_cache_lookup(ifnet_t *ifp, const uint32_t *ipaddr, struct rte_ether_addr *hwaddr)
{
	arp_entry_t *ac;

	ac = thmap_get(ifp->arp_cache, ipaddr, sizeof(uint32_t));
	if (ac == NULL) {
		return -1;
	}
	rte_ether_addr_copy(&ac->hwaddr, hwaddr);
	return 0;
}

/*
 * arp_request: construct an ARP REQUEST packet.
 *
 * => On success, returns an mbuf with Ethernet header; NULL otherwise.
 */
static bstgw_ethbuf_t *
arp_request(worker_t *worker __unused, const struct rte_ether_addr *src_hwaddr,
    const uint32_t *src_addr, const uint32_t *target)
{
	bstgw_ethbuf_t *m;
	struct rte_ether_hdr *eh;
	struct rte_arp_hdr *ah;
	struct rte_arp_ipv4 *arp;

	m = bstgw_ethpool_buf_alloc(NULL); //  rte_pktmbuf_alloc(worker->router->mbuf_pool);
	if (m == NULL) {
		return NULL;
	}
	eh = (struct rte_ether_hdr *)bstgw_ethbuf_head(m);
	//eh = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	m->l2_len = sizeof(struct rte_ether_hdr);
	m->l3_len = sizeof(struct rte_arp_hdr);
	m->data_len = m->l2_len + m->l3_len;
	//m->pkt_len = m->data_len;

	memset(&eh->d_addr, 0xff, sizeof(struct rte_ether_addr));
	rte_ether_addr_copy(src_hwaddr, &eh->s_addr);
	eh->ether_type = htons(RTE_ETHER_TYPE_ARP);

	/*
	 * ARP Ethernet REQUEST.
	 */
	ah = (struct rte_arp_hdr *)bstgw_ethbuf_data_ptr(m, m->l2_len);
	//ah = rte_pktmbuf_mtod_offset(m, struct rte_arp_hdr *, m->l2_len);
	ah->arp_hardware = htons(RTE_ARP_HRD_ETHER);
	ah->arp_protocol = htons(RTE_ETHER_TYPE_IPV4);
	ah->arp_hlen = RTE_ETHER_ADDR_LEN;
	ah->arp_plen = sizeof(struct in_addr);
	ah->arp_opcode = htons(RTE_ARP_OP_REQUEST);

	arp = &ah->arp_data;
	rte_ether_addr_copy(src_hwaddr, &arp->arp_sha);
	memcpy(&arp->arp_sip, src_addr, sizeof(arp->arp_sip));

	/* Broadcast message to look for the target. */
	memset(&arp->arp_tha, 0xff, sizeof(arp->arp_tha));
	memcpy(&arp->arp_tip, target, sizeof(arp->arp_tip));
	return m;
}

/*
 * arp_resolve: given the IPv4 address and an interface, both specified
 * by the route, resolve its MAC address in the Ethernet network.
 *
 * => Performs a lookup in the ARP cache or sends an ARP request.
 * => On success, return 0 and writes the MAC address into the buffer.
 * => On failure, return -1.
 */
int
arp_resolve(worker_t *worker, const route_info_t *rt,
    struct rte_ether_addr *hwaddr)
{
	const uint32_t *addr = (const void *)&rt->next_hop;
	bstgw_ethbuf_t *m;
	ifnet_t *ifp;
	int ret;

	if ((ifp = ifnet_getbyidx(rt->if_idx)) == NULL) {
		EMSG("[%s] packet route info has invalid index (%u)",
			worker->net_if->name, rt->if_idx);
		return -1;
	}

	/* Lookup in the ARP cache. */
	ret = arp_cache_lookup(ifp, addr, hwaddr);
	if (ret == 0) {
		return 0;
	}

	/* Construct an ARP request. */

	if (rt->if_addr_idx >= ifp->ips_cnt) {
		EMSG("rt->if_addr_idx invalid: %u (max valid: %u)",
			rt->if_addr_idx, ifp->ips_cnt - 1);
		panic("invalid if_addr_idx value");
	}

	const void *src_ip_ptr = (const void *)&ifp->if_ips[rt->if_addr_idx];
	m = arp_request(worker, &ifp->hwaddr, src_ip_ptr, addr);

	/* Send an ARP request. */
	if (m && pktq_enqueue(worker, rt->if_idx, m) == -1) {
		EMSG("[%s] ARP request enqueuing failed for index: %u",
			worker->net_if->name, rt->if_idx);
		return -1;
	}

	/* XXX: Just drop the packet for now; the caller will retry. */
	return -1;
}

static inline bool
arp_is_interesting(const struct rte_arp_hdr *ah, const ifnet_t *ifp, bool *targeted)
{
	const struct rte_arp_ipv4 *arp = &ah->arp_data;
	const struct rte_ether_addr *tha = &arp->arp_tha;
	bool ucast, bcast, untrusted;

	/* Unicast to us, broadcast or ARP probe? */
	ucast = rte_is_same_ether_addr(&ifp->hwaddr, &arp->arp_tha);
	bcast = rte_is_broadcast_ether_addr(tha) || rte_is_zero_ether_addr(tha);
	/* Special case: request from untrusted world - we always reply */
	untrusted = ifp->trust_status == BSTGW_IF_NONSEC;
	if (ucast || bcast || untrusted) {
		if(__predict_false( untrusted )) {
			*targeted = true;
		} else {
			/* Is the target IP matching the interface? */
			*targeted = false;
			for (unsigned int i=0; i<ifp->ips_cnt; i++) {
				if (memcmp(&ifp->if_ips[i],
				&arp->arp_tip, sizeof(arp->arp_tip)) == 0) {
					*targeted = true;
					break;
				}
			}
		}
		return true;
	}
	return false;
}

/*
 * arp_input: process an ARP packet.
 */
int
arp_input(worker_t *worker, bstgw_ethbuf_t *m)
{

	ifnet_t *ifp = worker->net_if;
	if (ifp == NULL) goto drop;

	struct rte_ether_hdr *eh;
	struct rte_arp_hdr *ah;
	struct rte_arp_ipv4 *arp;
	bool targeted;

	/*
	 * Get the ARP header and verify 1) hardware address type
	 * 2) hardware address length 3) protocol address length.
	 */
	ah = (struct rte_arp_hdr *)bstgw_ethbuf_data_ptr(m, m->l2_len);
	//ah = rte_pktmbuf_mtod_offset(m, struct rte_arp_hdr *, m->l2_len);
	if (ah->arp_hardware != htons(RTE_ARP_HRD_ETHER) ||
	    ah->arp_hlen != RTE_ETHER_ADDR_LEN ||
	    ah->arp_plen != sizeof(in_addr_t)) {

		EMSG("Failed ARP header verification step");
		goto drop;

	}
	arp = &ah->arp_data;


	if (!arp_is_interesting(ah, ifp, &targeted)) {
		goto drop;
	}

	/*
	 * ARP cache entry:
	 *
	 * => If target IP is us, then CREATE or UPDATE.
	 * => Otherwise, UPDATE (only if the entry already exists).
	 */
    /* TODO: if ARP Reply && ifp->trust_status == BSTGW_IF_NONSEC, but
     *       the resolved IP is not one of the trusted IPs, then don't cache
     *       to avoid cache poisoning by the untrusted world;
     *
     *       Note: the poisoning impact is very limited as the ARP caches are
     *       (output) interface-specific, i.e., it cannot (should not be
     *       possible to) redirect external packets at the moment. */
	arp_cache(ifp, &arp->arp_sip, &arp->arp_sha, targeted);

	/*
	 * If ARP REQUEST for us, then process it producing APR REPLY.
	 */
	if (targeted && ntohs(ah->arp_opcode) == RTE_ARP_OP_REQUEST) {
		const uint32_t ipaddr = arp->arp_tip; // copy

		/*
		 * Prepare an ARP REPLY.  Swap the source and target fields,
		 * both for the hardware and protocol addresses.
		 */
		ah->arp_opcode = htons(RTE_ARP_OP_REPLY);

		memcpy(&arp->arp_tha, &arp->arp_sha, sizeof(arp->arp_tha));
		memcpy(&arp->arp_tip, &arp->arp_sip, sizeof(arp->arp_tip));

		memcpy(&arp->arp_sha, &ifp->hwaddr, sizeof(arp->arp_sha));
		memcpy(&arp->arp_sip, &ipaddr, sizeof(arp->arp_sip));

		/* Update the Ethernet frame too. */
		eh = (struct rte_ether_hdr *)bstgw_ethbuf_data_ptr(m, 0);
		//eh = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
		rte_ether_addr_copy(&eh->s_addr, &eh->d_addr);
		rte_ether_addr_copy(&ifp->hwaddr, &eh->s_addr);

		if (pktq_enqueue(worker, ifp->ifnet_idx, m) == -1) {
			goto drop;
		}
		return 1; // consume
	}
drop:
	/*
	 * Drop the packet:
	 * - Free the packet.
	 */
	//rte_pktmbuf_free(m);
	bstgw_ethpool_buf_free(m);
	return -1;
}
