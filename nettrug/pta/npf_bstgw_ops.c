#include <sys/queue.h>
#include <string.h>

#include "bstgw_eth_buf.h"

#include <net/npf.h>
#include <npf_firewall/npfkern.h>

#include "npf_router.h"
#include "utils.h"


/*
 * BSTGW's physical and virtual network interfaces/devices.
 */

static const char *
bstgw_ifop_getname(npf_t *npf __unused, ifnet_t *ifp)
{
	return ifp->name;
}

static ifnet_t *
bstgw_ifop_lookup(npf_t *npf __unused, const char *ifname)
{
	unsigned int idx = ifnet_nametoindex(ifname);
	if (idx == 0) return NULL;

	return ifnet_getbyidx(idx);
}

static void
bstgw_ifop_flush(npf_t *npf __unused, void *arg)
{
	ifnet_flush_arg(arg);
}

static void *
bstgw_ifop_getmeta(npf_t *npf __unused, const ifnet_t *ifp)
{
	return ifp->arg;
}

static void
bstgw_ifop_setmeta(npf_t *npf __unused, ifnet_t *ifp, void *arg)
{
	ifp->arg = arg;
}

/*
 * BSTGW packet buf wrappers.
 */

static struct mbuf *
bstgw_mbuf_alloc(npf_t *npf __unused, unsigned flags __unused, size_t size)
{
	//bstgw_ethbuf_t *eb = bstgw_ethbuf_alloc_aligned(size, BSTGW_PRIV_SIZE, BSTGW_CACHE_LINE);
    bstgw_ethbuf_t *eb = bstgw_ethpool_buf_alloc(NULL);

	// align ethernet/ip
	if (eb) eb->data_off = 2 + sizeof(struct rte_ether_addr);

	// TODO: do we need to init the metadata somehow?

	return (struct mbuf *)eb;
}

static void
bstgw_mbuf_free(struct mbuf *m0)
{
	bstgw_ethbuf_t *pktbuf = (void *)m0;
	bstgw_ethpool_buf_free(pktbuf);
}

static void *
bstgw_mbuf_getdata(const struct mbuf *m0)
{
	const bstgw_ethbuf_t *pktbuf = (const void *)m0;
	return (void *)bstgw_ethbuf_data_ptr(pktbuf, 0);
}

static struct mbuf *
bstgw_mbuf_getnext(struct mbuf *m0)
{
	bstgw_ethbuf_t *pktbuf = (void *)m0;
	return (void *)pktbuf->next;
}

static size_t
bstgw_mbuf_getlen(const struct mbuf *m0)
{
	const bstgw_ethbuf_t *pktbuf = (const void *)m0;
	return bstgw_ethbuf_data_len(pktbuf);
}

static size_t
bstgw_mbuf_getchainlen(const struct mbuf *m0)
{
	const bstgw_ethbuf_t *pktbuf = (const void *)m0;
	return bstgw_ethbuf_total_pkt_len(pktbuf);
}

static bool
bstgw_mbuf_ensure_contig(struct mbuf **mp, size_t len)
{
	bstgw_ethbuf_t *pktbuf = (void *)*mp;

	if (len > bstgw_ethbuf_data_len(pktbuf) && bstgw_ethbuf_linearize(pktbuf) < 0) {
		return false;
	}
	return len <= bstgw_ethbuf_data_len(pktbuf);
}

/*
 * NPF ops vectors.
 */

static const npf_mbufops_t npf_mbufops = {
	.alloc			= bstgw_mbuf_alloc,
	.free			= bstgw_mbuf_free,
	.getdata		= bstgw_mbuf_getdata,
	.getnext		= bstgw_mbuf_getnext,
	.getlen			= bstgw_mbuf_getlen,
	.getchainlen		= bstgw_mbuf_getchainlen,
	.ensure_contig		= bstgw_mbuf_ensure_contig,
	.ensure_writable	= NULL,
};

static const npf_ifops_t npf_ifops = {
	.getname		= bstgw_ifop_getname,
	.lookup			= bstgw_ifop_lookup,
	.flush			= bstgw_ifop_flush,
	.getmeta		= bstgw_ifop_getmeta,
	.setmeta		= bstgw_ifop_setmeta,
};

npf_t *
npf_dpdk_create(int flags, npf_router_t *router)
{
	return npfk_create(flags, &npf_mbufops, &npf_ifops, router);
}
