#include "npf_router.h"

#include <assert.h>
#include <string.h>

#include <kernel/spinlock.h>
#include <initcall.h>

#include <nv.h>
#include <newlib/linux/sys/socket.h> // AF_INET
#include <newlib/arpa/nameser.h> // NS_INADDRSZ

#include "bstgw_netif_utils.h"

#define	MAX_IFNET_IDS		(32) // XXX: hardcoded, e.g. worker_t::bitmap

/* List of network device / interface */
struct ifnet_list netdev_list;

/* Index -> ifnet mapping for faster access */
static ifnet_t *ifnet_map[MAX_IFNET_IDS];
static unsigned int next_if_idx;

static TEE_Result netdev_init(void) {
	netdev_list.slock = SPINLOCK_UNLOCK;
	LIST_INIT(&netdev_list.list);
	next_if_idx = 1;
	IMSG("netdev list ready");
    return TEE_SUCCESS;
}
service_init(netdev_init);

/* previously 1 per device;
 * now can be >1 per device (e.g., 1 rx, 1 tx); */
unsigned int ifnet_napi_count(void) {
	unsigned int cnt = 0;
	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev;
	LIST_FOREACH(net_dev , &netdev_list.list, entry)
		cnt += net_dev->napi_count;
	cpu_spin_unlock(&netdev_list.slock);
	return cnt;
}

unsigned int ifnet_dev_count(void) {
	return next_if_idx - 1; // todo: int read is atomic, right? else take spinlock
}

int	ifnet_dev_init(ifnet_t *netdev) {
	assert(netdev);
	memset(netdev, 0, sizeof(ifnet_t));

	netdev->egress_queue = calloc(1, sizeof(pktqueue_t));
	if (!netdev->egress_queue) return -1;

	netif_tx_stop_queue(netdev->egress_queue); // stopped by default
	return 0;
}

int ifnet_dev_register(ifnet_t *netdev) {
	assert(netdev);
	if (next_if_idx > MAX_IFNET_IDS) {
		EMSG("Maxiumum number of supported netdev reached");
		return -1;
	}
	cpu_spin_lock(&netdev_list.slock);
	LIST_INSERT_HEAD(&netdev_list.list, netdev, entry);
	ifnet_map[next_if_idx-1] = netdev;
	netdev->ifnet_idx = next_if_idx++;
	DMSG("New ifnet registered: %s (idx: %d)", netdev->name, netdev->ifnet_idx);
	cpu_spin_unlock(&netdev_list.slock);
	return 0;
}

unsigned int ifnet_nametoindex(const char *ifname) {
	int ret = 0;
	if (ifname == NULL) return ret;

	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev;
	LIST_FOREACH(net_dev , &netdev_list.list, entry) {
		if (strcmp(net_dev->name, ifname) == 0) {
			ret = net_dev->ifnet_idx;
			assert(ret >= 0);
			break;
		}	
	}
	cpu_spin_unlock(&netdev_list.slock);
	return ret;
}

ifnet_t * ifnet_getbyidx(unsigned int idx) {
	if (idx == 0 || idx >= next_if_idx) return NULL;
	// deleting entries is not supported atm, so no need to lock
	return ifnet_map[idx-1];
}

ifnet_t * ifnet_getbyname(const char *ifname) {
	if(!ifname) return NULL;
	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev;
	LIST_FOREACH(net_dev , &netdev_list.list, entry) {
		if (strcmp(net_dev->name, ifname) == 0) {
			cpu_spin_unlock(&netdev_list.slock);
			return net_dev;
		}
	}
	cpu_spin_unlock(&netdev_list.slock);
	return NULL;
}

void ifnet_flush_arg(void *arg) {
	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev;
	LIST_FOREACH(net_dev , &netdev_list.list, entry) net_dev->arg = arg;
	cpu_spin_unlock(&netdev_list.slock);	
}


/*
 * netif nvlist {
 * 	  name: 	 	str
 * 	  idx:  	 	number
 *    ip0:			nvlist {
 *	    addr_type: 	number
 *  	ip_addr_nbo:  number
 * 	  }
 *    ip1: ...
 * 	  ...
 * //TODO trusted:   	bool
 * //TODO flags:		number (e.g. IFF_UP)
 * }
 */
static nvlist_t *ifnet_dev_get_description(ifnet_t *ifp) {
	nvlist_t *descr = nvlist_create(0);
	if(!descr) return NULL;

	nvlist_add_string(descr, "name", ifp->name);
	nvlist_add_number(descr, "idx", ifp->ifnet_idx);

	nvlist_t *next_addr;
	unsigned int ip_idx;

	for( ip_idx = 0; ip_idx < BSTGW_MAX_NETIF_IPS; ip_idx++ ) {
		next_addr = nvlist_create(0);
		if (!next_addr) {
			EMSG("OOM");
			break;
		}

		nvlist_add_number(next_addr, "addr_type", AF_INET);

		// network byte order
		uint32_t ip4_nbo;
		memcpy((uint8_t *)&ip4_nbo, &ifp->if_ips[ip_idx], NS_INADDRSZ);
		// TODO: what happens with ip4_nbo? (it's not uint64_t)
		nvlist_add_number(next_addr, "ip_addr_nbo", ip4_nbo);

		char name[15];
		snprintf(name, sizeof(name), "ip%u", ip_idx);
		nvlist_move_nvlist(descr, name, next_addr);
	}

	// TODO
	//nvlist_add_bool(descr, "trusted", ifp->is_trusted);

	//nvlist_dump_ta(descr);
	return descr;
}

/*
 * nvlist {
 *    "ifs_list":	null
 * 	  <netif_name>: nvlist
 *    <netif_name>: nvlist
 * }
 */
nvlist_t *ifnet_get_descr_list(void) {
	nvlist_t *descr_list = nvlist_create(0);
	if(!descr_list) return NULL;

	nvlist_add_null(descr_list, "ifs_list");

	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev;
	LIST_FOREACH(net_dev , &netdev_list.list, entry) {
		nvlist_t *descr = ifnet_dev_get_description(net_dev);
		if(!descr) {
			EMSG("Warning: descriptor creation failed for index, prob. OOM");
			nvlist_destroy(descr_list);
			descr_list = NULL;
			break;
		}
		nvlist_move_nvlist(descr_list, net_dev->name, descr);
	}
	cpu_spin_unlock(&netdev_list.slock);

	if(nvlist_error(descr_list)) {
		EMSG("There was an error during netif nvlist creation");
		nvlist_destroy(descr_list);
		descr_list = NULL;
	}

	//nvlist_dump_ta(descr_list);
	return descr_list;
}