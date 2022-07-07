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

#ifndef _NPF_ROUTER_H_
#define _NPF_ROUTER_H_

#include <sys/queue.h>
#include <inttypes.h>

#include <dpdk/rte_ether.h>
#include <thmap.h>

//#include <net/if.h>
#include <net/npf.h>
#include <npf_firewall/npfkern.h>

// op-tee
#include <kernel/spinlock.h>
#include <tee_api_types.h>

#include <bstgw/worker_storage.h>
#include <bstgw/worker_binding.h>

#include <notifier.h>
#include <napi.h> // struct napi_struct;

#include "bstgw_eth_buf.h"
#include "bstgw_pktq.h"

/* Activates check that VNIC does not perform source IP spoofing.
 * Concrete:  compares VNIC srcIPs against those of the external
 *            trusted router interface and drops the packet if it
 *            does not match any
 *
 * FIXME:  optimize performance of the implementation
 *
 * Note:  can be implemented more efficiently using a firewall rule;
 *        the firewall can use connection tracking to avoid checks
 *        on already ESTABLISHED connections to reduce the overhead;
 *
 *        a firewall rule can also whitelist additional source IPs
 *        if that is desired in a given use-case */
//#define BSTGW_NON_NPF_ANTI_IP_SPOOFING

struct ifnet;
struct worker;

typedef int rx_handler_t(struct ifnet *, bstgw_ethbuf_t **pkts, unsigned int pktcount);
typedef int xmit_handler_t(struct ifnet *, bstgw_ethbuf_t **pkts, unsigned int pktcount);
typedef int notify_handler_t(struct ifnet *);

typedef struct route_table route_table_t;

#define IF_NAMESIZE 		(16)

/*
 * NPF router structures.
 */

typedef struct {
	unsigned		if_idx;
	unsigned		addr_len;
	npf_addr_t		next_hop;
	unsigned		if_addr_idx;   // IP to use for ARP requests (0:default)
} route_info_t;

#define	MBUF_NPF_NEED_L2	0x01

typedef enum { BSTGW_IF_NONSEC, BSTGW_IF_EXTERNAL } bstgw_trust_t;

typedef struct {
	unsigned		flags;
	unsigned		ether_type;
	route_info_t	route;
	unsigned 		out_if_idx;
} npf_mbuf_priv_t;

/* TODO: from netdev_features.h */
enum {
	NETIF_F_SG_BIT,			/* Scatter/gather IO. */
	NETIF_F_IP_CSUM_BIT,		/* Can checksum TCP/UDP over IPv4. */
	NETIF_F_HW_VLAN_CTAG_RX_BIT,	/* Receive VLAN CTAG HW acceleration */
	NETIF_F_RXCSUM_BIT,		/* Receive checksumming offload */
};

#define __NETIF_F_BIT(bit)	((netdev_features_t)1 << (bit))
#define __NETIF_F(name)		__NETIF_F_BIT(NETIF_F_##name##_BIT)

#define NETIF_F_HW_VLAN_CTAG_RX	__NETIF_F(HW_VLAN_CTAG_RX)
#define NETIF_F_IP_CSUM			__NETIF_F(IP_CSUM)
#define NETIF_F_RXCSUM			__NETIF_F(RXCSUM)
#define NETIF_F_SG				__NETIF_F(SG)
/* */

typedef struct ifnet {
	LIST_ENTRY(ifnet)	entry;
	unsigned int		ifnet_idx; // for router ifnet_map

	/* Device Part */
	struct rte_ether_addr	hwaddr; // TODO: add and copy @ VNIC/FEC driver init/probe
	void * 			priv_data; // like Linux's net_device priv

	/* Somewhere in the middle ... (todo: refactor) */
	bstgw_trust_t	trust_status;

#define BSTGW_MAX_NETIF_NAPI 	2
	struct napi_struct *napis[BSTGW_MAX_NETIF_NAPI];
	unsigned int 	napi_count;
	// pass down to device
	xmit_handler_t	*xmit_handler;
	// pass up to router
	rx_handler_t 	*rx_handler;
	
	// notify about new egress packets
	notify_handler_t *egress_ntfy_handler;

	char			name[IF_NAMESIZE];

//	void *			rx_handler_data; // cf. last rx_handler() arg
#define BSTGW_MAX_NETIF_WORKER 	BSTGW_MAX_NETIF_NAPI
	struct worker *		workers[BSTGW_MAX_NETIF_WORKER];
	unsigned int		worker_count;

	/* Router/Netif Part */
#define BSTGW_MAX_NETIF_IPS 	2
	npf_addr_t      if_ips[BSTGW_MAX_NETIF_IPS]; // 0->default; rest: additional
	unsigned int	ips_cnt; // number of defined if_ips (should be >=1)

	void *			arg;//todo (I think this one will hold the NPF ifnet ID!)
	thmap_t *		arp_cache;
	uint64_t 		features; // TODO: new
	pktqueue_t *	egress_queue;
} ifnet_t;

static inline void *netdev_priv(const struct ifnet *dev) {
	assert(dev);
	return dev->priv_data;
}

extern struct ifnet_list {
	unsigned int slock;
	LIST_HEAD(, ifnet) list;
} netdev_list;

typedef struct npf_router {
	npf_t *			npf;
	//struct rte_mempool *	mbuf_pool;
	unsigned		pktqueue_size;
	route_table_t *		rtable;

	struct notifier notfer;

#ifdef BSTGW_NON_NPF_ANTI_IP_SPOOFING
#define BSTGW_MAX_EXT_NDEVS 2
	unsigned 		ext_ndevs_count;
	struct ifnet_t *	ext_ndevs[BSTGW_MAX_EXT_NDEVS];
#endif

	unsigned		worker_count;
	struct worker *		worker[];
} npf_router_t;

typedef enum { BSTGW_WRK_INOUT = 0, BSTGW_WRK_OUT = 1 } worker_type_t;

typedef struct worker {
	wrk_ls_t worker_npf_data;
	int worker_id; // unused atm

	npf_t *			npf;
	npf_router_t *	router;

	worker_bind_t 	bind;

	ifnet_t * 		net_if;
	unsigned int 	napi_idx;
} worker_t;

/*
 * NPF DPDK operations, network interface and configuration loading.
 */

npf_t *		npf_dpdk_create(int, npf_router_t *);

/* new (TODO) */
int		ifnet_dev_init(ifnet_t *netdev);
int		ifnet_dev_register(ifnet_t *netdev);

unsigned int ifnet_napi_count(void);
unsigned int ifnet_dev_count(void);

unsigned int ifnet_nametoindex(const char *ifname);
ifnet_t * ifnet_getbyidx(unsigned int idx);
ifnet_t * ifnet_getbyname(const char *ifname);

void ifnet_flush_arg(void *arg);

nvlist_t *ifnet_get_descr_list(void);
/*  */


int 	ifnet_setup(npf_router_t *router, ifnet_t *);

int		ifnet_ifattach(npf_router_t *, ifnet_t *);
void		ifnet_ifdetach(npf_router_t *, ifnet_t *);
//void		ifnet_put(ifnet_t *);

int		load_config(npf_router_t *);

/*
 * Routing and ARP.
 */

route_table_t *	route_table_create(void);
void		route_table_destroy(route_table_t *);

int		route_add(route_table_t *, const void *, unsigned, unsigned,
		    const route_info_t *);
int		route_lookup(route_table_t *, const void *, unsigned,
		    route_info_t *);

int		arp_resolve(worker_t *, const route_info_t *, struct rte_ether_addr *);
int		arp_input(worker_t *, bstgw_ethbuf_t *);

/*
 * Worker / processing.
 */

int worker_get_number(void); // called by NW
worker_t *bind_to_worker(void); // called by NW
void detach_nw_from_worker(worker_t *); // called by NW

int worker_run(worker_t *); // called by NW

int     pktq_prepare_enqueue(worker_t *, unsigned, bstgw_ethbuf_t *);
int		pktq_do_menqueue(worker_t *, unsigned, bstgw_ethbuf_t **, unsigned int);
int     pktq_enqueue(worker_t *, unsigned , bstgw_ethbuf_t *);

void	router_if_input(worker_t *, bstgw_ethbuf_t **, unsigned int);
int     router_pktq_tx(ifnet_t *);

uint32_t router_in_burst_size(ifnet_t *, ifnet_t *);

extern npf_router_t *trusted_router;

/*
 * NPF Worker.
 */
wrk_ls_t *bind_to_npfworker(void);
void npfworker_run(void);
void detach_nw_from_npfworker(void);

/*
 * NPF Config.
 */
#include <npf_firewall/npf_impl.h>

int bstgw_npfctl_cmd(uint64_t, nvlist_t *, nvlist_t *);

#endif
