/*
 * Virtio Network Device
 *
 * Copyright Fabian Schwarz (CISPA Helmholtz Center for Information Security) 2022
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Fabian Schwarz    <fabian.schwarz@cispa.saarland>
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <internal/uio.h>
#include <internal/virtio.h>

/* TODO
#include "net/net.h"
#include "net/checksum.h"
*/

#include <napi.h>
#include <notifier.h> // bsgtgw

#include <virtio/virtio_net.h>
#include <internal/virtio-net.h>

#include <internal/vnic-mmio_private.h>

#include <internal/vnic.h>

#include <internal/vqueue.h>
#include <internal/uio.h>
#include <internal/err.h>

#include <stdlib.h>

//#define BSTGW_VNET_DEBUG


/* for now, only allow larger queues; with virtio-1, guest can downsize */
#define VIRTIO_NET_RX_QUEUE_MIN_SIZE VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE
#define VIRTIO_NET_TX_QUEUE_MIN_SIZE VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE

#define VIRTIO_NET_IP4_ADDR_SIZE   8        /* ipv4 saddr + daddr */

#define VIRTIO_NET_TCP_FLAG         0x3F
#define VIRTIO_NET_TCP_HDR_LENGTH   0xF000

/* IPv4 max payload, 16 bits in the header */
#define VIRTIO_NET_MAX_IP4_PAYLOAD (65535 - sizeof(struct ip_header))
#define VIRTIO_NET_MAX_TCP_PAYLOAD 65535

/* header length value in ip header without option */
#define VIRTIO_NET_IP4_HEADER_LENGTH 5

#define VIRTIO_NET_IP6_ADDR_SIZE   32      /* ipv6 saddr + daddr */
#define VIRTIO_NET_MAX_IP6_PAYLOAD VIRTIO_NET_MAX_TCP_PAYLOAD

//#define VIRTIO_NET_RSC_DEFAULT_INTERVAL 300000


void virtio_net_get_config(vdev_t *vdev, uint8_t *config)
{
    assert(config);

    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));
    vnet_t *n = &vnic->vnet;

    struct virtio_net_config netcfg;
    memset(&netcfg, 0 , sizeof(struct virtio_net_config));
    
    netcfg.status = n->status;
    netcfg.max_virtqueue_pairs = 1; //n->max_queues;
    netcfg.mtu = n->net_conf.mtu;
    memcpy(netcfg.mac, n->mac.addr_bytes, RTE_ETHER_ADDR_LEN);

    if(n->config_size != sizeof(netcfg)) panic("memcpy size mismatch!");
    memcpy(config, &netcfg, n->config_size);
}

void virtio_net_set_config(vdev_t *vdev __unused, const uint8_t *config __unused)
{
    // we must not allow MTU changes by the untrusted world and do not implement
    // any ctrl channels at the moment
}

static bool virtio_net_started(vnet_t *n, uint8_t status)
{
    return (status & VIRTIO_CONFIG_S_DRIVER_OK) &&
        (n->status & VIRTIO_NET_S_LINK_UP); //&& vdev->vm_running;
}


// TODO
void virtio_net_set_status(vdev_t *vdev, uint8_t status)
{
    assert(vdev);
    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));
    vnet_t *n = &vnic->vnet;

    assert(n->vqs);

    uint8_t queue_status = status;
    //vnet_q_t *q = &n->vqs;

    bool queue_started = virtio_net_started(n, queue_status);


    if (queue_started) {
        //trusted_router_flush_queued_packets(n);

    }

/*
    if (!q->tx_waiting) {
        continue;
    }
*/

    if (queue_started) {
        napi_schedule(&vnic->napi);
        //qemu_bh_schedule(q->tx_bh);
    } else {
        //qemu_bh_cancel(q->tx_bh);

        if ((n->status & VIRTIO_NET_S_LINK_UP) == 0 &&
            (queue_status & VIRTIO_CONFIG_S_DRIVER_OK) //&& vdev->vm_running
            ) {
            /* if tx is waiting we are likely have some packets in tx queue
                * and disabled notification */
           // q->tx_waiting = 0;
           // virtio_queue_set_notification(q->tx_vq, 1);
           // virtio_net_drop_tx_queue_data(vdev, q->tx_vq);
        }
    }
}

/*static */void virtio_net_set_link_status(vnet_t *n, bool link_up)
{
    uint16_t old_status = n->status;

    if (link_up)
        n->status |= VIRTIO_NET_S_LINK_UP;
    else
        n->status &= ~VIRTIO_NET_S_LINK_UP;

    if (n->status != old_status)
        virtio_notify_config(n->vdev);

    virtio_net_set_status(n->vdev, n->vdev->status); //?
}

void virtio_net_reset(vdev_t *vdev __unused)
{
    //vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));
    //vnet_t *n = &vnic->vnet;

    // TODO: disable NAPI?

    /* Flush any pending queue packets? / Disable trusted router interface? */
    // todo
}

static uint64_t virtio_net_guest_offloads_by_features(uint32_t features)
{
    // TODO: support + enable CSUM
    static const uint64_t guest_offloads_mask = 0;
    //    (1ULL << VIRTIO_NET_F_GUEST_CSUM);

    return guest_offloads_mask & features;
}


void virtio_net_set_features(vdev_t *vdev, uint64_t features)
{
    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));
    vnet_t *n = &vnic->vnet;

    n->curr_guest_offloads =
        virtio_net_guest_offloads_by_features(features);
    //virtio_net_apply_guest_offloads(n);
}


/* RX */

static inline bool virtio_net_can_receive(vnet_q_t *q)
{
    vnet_t *n = q->n;
    if(unlikely( !q->rx_vq->vring_addr ||
        !(n->vdev->status & VIRTIO_CONFIG_S_DRIVER_OK) )) {
        return false;
    }

    return true;
}


// TODO
#define MAX_RX_IOVS 1

#ifdef BSTGW_VNIC_DBG_STATS
unsigned long int full_nw_recv = 0;
#endif

// note: we do NOT support receive buffer merging and chained buffers atm
// TODO: handle error cases -- best would be to re-enqueue the VQ rx avail buf?
//       or add an 0-length buffer to USED?
ssize_t virtio_net_receive(vnet_q_t *q, bstgw_ethbuf_t **ethbufs, unsigned int pktcount)
{

    assert(q && ethbufs);
    vnet_t *n = q->n;
    assert (n);
    vdev_t *vdev = n->vdev;
    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));

    // TODO: can we kick that somehow?
    if(unlikely( !virtio_net_can_receive(q) )) {
        return -1;
    }

    struct vqueue *rx_vq = q->rx_vq;
    ssize_t processed = 0, enqueued = 0;

    //uint16_t space = vqueue_num_of_avail_buf_and_notify(rx_vq);
    uint16_t space = vqueue_num_of_avail_buf(rx_vq);
    //if (!space) return 0;
    if (!space) return VIRTIO_NO_AVAIL;

    if (space < (VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE / 2)) {
        return VIRTIO_NAPI_RETRY;
    }

    /* prepare iovec buffer */
    paddr_t out_phys[MAX_RX_IOVS];
    iovec_kern_t out_iovs[MAX_RX_IOVS];
    struct vqueue_buf buf;

    memset(&buf, 0, sizeof(buf));
    buf.out_iovs.cnt  = MAX_RX_IOVS; // TODO: vs used?
    buf.out_iovs.phys = out_phys;
    buf.out_iovs.iovs = out_iovs;


    // virtio vnet header
    struct virtio_net_hdr_v1 vnet_hdr;
    memset(&vnet_hdr, 0, sizeof(vnet_hdr));
    vnet_hdr.flags = 0; // TODO: add checksum support
    vnet_hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE; // no GSO support yet
    vnet_hdr.num_buffers = 1; // bcs. no F_MRG_RXBUF


    //if (!receive_filter(n, ethbuf, size))
    //    return size;
    
    for (unsigned int buf_idx=0; buf_idx < pktcount; buf_idx++) {
        /* Inspect next ethbuf */
        bstgw_ethbuf_t *next_buf = ethbufs[buf_idx];
        if(unlikely( !next_buf )) {
            processed++;
            EMSG("Error: NULL entry in ethbuf array");
            continue;
        }

        // TODO: we currently do not support buffer chains, yet
        if(unlikely( bstgw_ethbuf_linearize(next_buf) != 0 )) {
            EMSG("Chained, non-linearizable ethernet buffers not yet supported by VNIC. Drop.");
            // drop
            bstgw_ethpool_buf_free(next_buf);
            processed++;
            continue;
        }

        size_t size = bstgw_ethbuf_data_len(next_buf);
        if(unlikely( !size )) {
            IMSG("Warning: Ethernet buffer with data len 0. Drop.");
            // drop
            bstgw_ethpool_buf_free(next_buf);
            processed++;
            continue;
        }
        uint8_t *data_ptr = bstgw_ethbuf_data_ptr(next_buf, 0);
        assert(data_ptr);


        /* Grab AVAIL VQ buf */
        int ret;

        ret = vqueue_get_avail_buf(rx_vq, &buf);
        if (ret == ERR_NOT_ENOUGH_BUFFER) {
#ifdef BSTGW_VNIC_DBG_STATS
            full_nw_recv++;
#endif
            break;
        } else if (ret != NO_ERROR) {
            EMSG("vqueue_get_avail_buf() failed with error code: %d", ret);
            // todo: return error value?
            break;
        }


        // TODO: we currently do NOT support receive buffer merging, yet,
        // i.e., either exactly 1 VQ buffer is fits the buffer, or it will be dropped.


       /* we will need at least 1 (write-only) iovec */
        if (unlikely(buf.out_iovs.used < 1)) {
            EMSG("unexpected out_iovs num %d\n", buf.out_iovs.used);
            vqueue_add_buf(rx_vq, &buf, 0); // todo: is that fine?
            // todo: return ERR_INVALID_ARGS;
            break;
        }

        /* there should be exactly 1 in_iov but it is not fatal if the first
            one is big enough */
        //if (buf->out_iovs.used != 1) {
        //    EMSG("unexpected out_iovs num %d\n", buf->out_iovs.used);
        //}


        /* in_iovs are not used for NW's rx */
        if (unlikely(buf.in_iovs.used != 0)) {
            EMSG("unexpected in_iovs num %d\n", buf.in_iovs.used);
        }


        /* check if buffer can hold full data (only 1 buffer w/o mrg_rxbuf) */
        /* TODO:
         * this check should be moved BEFORE we start de-queuing VQ buffers!
         *  --> add a vqueue API for that!
         */
        // w/o mrg_rxbuf, just check first buffer
        if(unlikely( buf.out_iovs.iovs[0].len < (sizeof(vnet_hdr) + size) )) {
            EMSG("Avail Buffer not big enough to hold header + data: %u", buf.out_iovs.iovs[0].len);
            vqueue_add_buf(rx_vq, &buf, 0); // todo: is that fine?
            // todo: ?/ERR_NOT_ENOUGH_BUFFER;
            break;
        }

        /* TODO: Check that the iovec buffer pointers to not point into secure
         * memory (and securely cache the iovec sizes).
         */

        ret = vqueue_map_iovs(&buf.out_iovs, 0);
        if (unlikely(ret)) {
            EMSG("failed to map iovs %d\n", ret);
            vqueue_add_buf(rx_vq, &buf, 0); // todo: is that fine?
            // todo: return ret;
            break;
        }
        
        /* copy in header */
        // note: we may only use 1/num_buffers iovs
        if(unlikely( membuf_to_kern_iovec(buf.out_iovs.iovs, vnet_hdr.num_buffers,
            (uint8_t *)&vnet_hdr, sizeof(vnet_hdr)) != sizeof(vnet_hdr) )) {
            EMSG("header copying failed");
            vqueue_unmap_iovs(&buf.out_iovs);
            vqueue_add_buf(rx_vq, &buf, 0); // todo: is that fine?
            // todo: return ?/ERR_NO_MEMORY;
            break;
        }

        /* copy in data (after header) */
        if(unlikely( membuf_to_kern_iovec_skip_begin(buf.out_iovs.iovs, vnet_hdr.num_buffers,
            data_ptr, size, sizeof(vnet_hdr)) != size )) {
            EMSG("data copying failed");
            vqueue_unmap_iovs(&buf.out_iovs);
            vqueue_add_buf(rx_vq, &buf, 0); // todo: is that fine?
            // todo: return ?/ERR_NO_MEMORY;
            break;
        }

        vqueue_unmap_iovs(&buf.out_iovs);

        /* push to other side */
        vqueue_add_buf(rx_vq, &buf, sizeof(vnet_hdr) + size);

        bstgw_ethpool_buf_free(next_buf);


        enqueued++;
        if (++processed >= n->rx_burst) {
            break;
        }
    }

    if(likely( enqueued > 0 )) {
        vqueue_kick(rx_vq);
    }

#ifdef BSTGW_VNET_DEBUG
    DMSG("receive return with processed: %ld (of %u), %ld enqueued",
        processed, pktcount, enqueued);
#endif
    return processed;
}


/* TX */

// TODO: Linux has 16 + 2 for 4kB page size
#define MAX_TX_IOVS 4

// TODO: adapt to Ethernet MTU
#define MAX_PCKT_LEN (2048)

// small perf. optimization
static ifnet_t *fec_ifp = NULL;

//static
int32_t virtio_net_flush_tx(vnet_q_t *q)
{
    assert(q);
    vnet_t *n = q->n;
    assert (n);

    struct vqueue *tx_vq = q->tx_vq;
    vdev_t *vdev = n->vdev;
    // todo: could move net_device to vnet
    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));

    assert(vdev && tx_vq && vnic);

    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return 0;
    }

    //uint16_t data_avail = vqueue_num_of_avail_buf_and_notify(tx_vq);
    uint16_t data_avail = vqueue_num_of_avail_buf(tx_vq);
    //if (!data_avail) return 0;
    if (!data_avail) return VIRTIO_NO_AVAIL;


    /* prepare iovec buffer */
    paddr_t in_phys[MAX_TX_IOVS];
    iovec_kern_t in_iovs[MAX_TX_IOVS];
    struct vqueue_buf buf;

    memset(&buf, 0, sizeof(buf));
    buf.in_iovs.cnt  = MAX_TX_IOVS;
    buf.in_iovs.phys = in_phys;
    buf.in_iovs.iovs = in_iovs;

    unsigned int num_packets = 0;
    unsigned int to_be_sent = 0;

    // at least 1, bcs. loop has do-while logic
    // (+ we need extra notifications if we do nothing on full queue)
    if(unlikely( !fec_ifp )) fec_ifp = ifnet_getbyname("imx6q-fec");
    uint32_t ref_burst = router_in_burst_size(&vnic->net_device, fec_ifp);

    if (ref_burst < (VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE / 2)) {
        return VIRTIO_NAPI_RETRY;
    }

    uint32_t burst = (n->tx_burst <= ref_burst) ? n->tx_burst : ref_burst;
    burst = (burst > data_avail) ? data_avail : burst;

    // note: 1kB seems too big for small OP-TEE stacks
    //bstgw_ethbuf_t *frames[burst];
    // TODO: too slow?!
    bstgw_ethbuf_t **frames = malloc(sizeof(bstgw_ethbuf_t *) * burst);
    if (!frames) {
        EMSG("Out of memory!: couldn't allocate frame pointer array");
        return ERR_NO_MEMORY;
    }


    /* send loop (Linux -> vnic -> router) */
    for (;;) {
        // TODO: alloc special buffer struct (from heap or pool)
        bstgw_ethbuf_t *eth_frame = bstgw_ethpool_buf_alloc(NULL);
        if( unlikely(!eth_frame) ) {
            EMSG("WARNING: Failed to allocate buffer for new packet");
            //ret = ERR_NO_MEMORY;
            // TODO: how to handle? shouldn't we send out the existing stuff?
            //      (at least if only buffer pool is empty, instead of full OOM)
            break; // will flush existing
        }

        // align
        eth_frame->data_off = 2;
        eth_frame->packet_type = RTE_PTYPE_L2_MASK; // todo: set to actual value

        int ret;
        ret = vqueue_get_avail_buf(tx_vq, &buf);
        if (ret == ERR_NOT_ENOUGH_BUFFER) {
            bstgw_ethpool_buf_free(eth_frame);
            break;
        } else if (ret != NO_ERROR) {
            EMSG("vqueue_get_avail_buf() failed with error code: %d", ret);
            bstgw_ethpool_buf_free(eth_frame);
            // TODO: send instead?
            for (unsigned int i=0; i<to_be_sent; i++) bstgw_ethpool_buf_free(frames[i]);
            free(frames);
            return ret;
        }

        /* we will need at least 1 (read-only) iovec */
        if (unlikely(buf.in_iovs.used < 1)) {
            EMSG("unexpected in_iovs num %d\n", buf.in_iovs.used);
            bstgw_ethpool_buf_free(eth_frame);
            // TODO: send instead?
            for (unsigned int i=0; i<to_be_sent; i++) bstgw_ethpool_buf_free(frames[i]);
            free(frames);
            // TODO: put buf back into used table?
            return ERR_INVALID_ARGS;
        }

        /* out_iovs are not used for NW's tx */
        if (unlikely(buf.out_iovs.used != 0)) {
            EMSG("unexpected out_iovs num %d\n", buf.out_iovs.used);
        }


        // map to get VAs
        ret = vqueue_map_iovs(&buf.in_iovs, 0);
        if (unlikely(ret)) {
            EMSG("failed to map iovs %d\n", ret);
            bstgw_ethpool_buf_free(eth_frame);
            // TODO: send instead?
            for (unsigned int i=0; i<to_be_sent; i++) bstgw_ethpool_buf_free(frames[i]);
            free(frames);
            // TODO: put buf back into used table?
            return ret;
        }


        const volatile struct virtio_net_hdr_v1 vnet_hdr;
        size_t total_pckt_len = buf.in_iovs.len - sizeof(vnet_hdr);


        /* check message size (TODO: could check packet size) */
        // expect 1 buffer to at least hold the full vnet header
        if (unlikely(buf.in_iovs.iovs[0].len < sizeof(vnet_hdr))) {
            EMSG("msg too short %zu\n", buf.in_iovs.iovs[0].len);
            bstgw_ethpool_buf_free(eth_frame);
            goto drop;
        }


        // Check for data
        if( unlikely(total_pckt_len == 0) ) {
            EMSG("No packet data");
            bstgw_ethpool_buf_free(eth_frame);
            goto drop;
        }

        // Check for minimal data size (TODO: including FCS or not? ARP is smaller?!)
        //if( unlikely(total_pckt_len < 64) ) {
        // 4@: Eth hdr (16) + ARP hdr (28);  or: Eth hdr + IP hdr (20) + ICMP hdr (8) [no padding/whatever]
        if( unlikely(total_pckt_len < 42) ) {
            EMSG("Ethernet frame too small (data: %u)", total_pckt_len);
            bstgw_ethpool_buf_free(eth_frame);
            goto drop;
        }

        // Check for max. len
        // TODO: what is our max supported length?! (base it on Ethernet MTU)
        /* '- 2' because of ethernet-ip header alignment */
        if( unlikely(total_pckt_len > (BSTGW_DEFAULT_BUF_SIZE - 2)) ) {
            EMSG("Too big packet buffer len: %u", total_pckt_len);
            bstgw_ethpool_buf_free(eth_frame);
            goto drop;
        }


        /* TODO: Check that iovec addresses are not pointing to secure memory
         * and securely cache the iovec sizes to prevent TOCTOU.
         */


        /* Copy the header */
        if (unlikely(kern_iovec_to_membuf((uint8_t *)&vnet_hdr, sizeof(vnet_hdr), 
            buf.in_iovs.iovs, buf.in_iovs.used) != sizeof(vnet_hdr))) {
            EMSG("header copying failed");
            bstgw_ethpool_buf_free(eth_frame);
            ret = ERR_INVALID_ARGS;
            goto tx_error;
        }


        // Check the header (TODO: later want to check for checksumming and set
        // a metadata flag in the buffer passed to the router)
        // Note: while .num_buffers must be 0 for tx packets according to VirtIO spec,
        // it sometimes was set to values != 0, so ignore it here
        if (unlikely(vnet_hdr.flags != 0 || vnet_hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE)) {
            //|| vnet_hdr.num_buffers != 0)) {
            EMSG("invalid virtio-net header value(s):");
            EMSG("vnet_hdr.flags == %d, .gso_type == %d, .num_buffers == %d",
                vnet_hdr.flags, vnet_hdr.gso_type, vnet_hdr.num_buffers);
            bstgw_ethpool_buf_free(eth_frame);
            ret = ERR_INVALID_ARGS;
            goto tx_error;
        }


        /* Copy the data (w/o header) */

        eth_frame->data_len = total_pckt_len;


        if( unlikely(kern_iovec_to_membuf_skip_begin(bstgw_ethbuf_data_ptr(eth_frame, 0), total_pckt_len,
            buf.in_iovs.iovs, buf.in_iovs.used, sizeof(vnet_hdr)) != total_pckt_len ) ) {
            EMSG("Failed to copy packet data into secure buffer");
            ret = ERR_GENERIC;
            bstgw_ethpool_buf_free(eth_frame);
            goto tx_error;
        }


        // just enqueue and increase counter
        frames[to_be_sent++] = eth_frame;
#ifdef BSTGW_VNET_DEBUG
        DMSG("%u packets enqueued for processing", to_be_sent);
#endif

        /* Note: do NOT free eth_frame, because it will result in a double-free crash,
         * bcs. either it will be forwarded and later freed after sent,
         * or it will be dropped and thereby freed by the router
         */

drop:
        vqueue_unmap_iovs(&buf.in_iovs);


        ret = vqueue_add_buf(tx_vq, &buf, sizeof(vnet_hdr) + total_pckt_len);
        if(unlikely( ret != NO_ERROR ))
            EMSG("vqueue_add_buf() at vnet-tx returned error: %u", ret);

        // TODO!: batch
//        vqueue_kick(tx_vq); // TODO: move after branch? (note: anyway disabled most of the time -- BUT this might not hold true during high-load tests)


        //if (++num_packets >= n->tx_burst) {
        if (++num_packets >= burst) {
            break;
        }
        continue;

tx_error:
        vqueue_unmap_iovs(&buf.in_iovs);
        // TODO: sent instead?
        for (unsigned int i=0; i<to_be_sent; i++) bstgw_ethpool_buf_free(frames[i]);
        free(frames);
        // TODO: put buf back into used table? (API missing atm)
        return ret;
    }

    // kick NW
    if(likely( num_packets > 0 )) {
        vqueue_kick(tx_vq);
    }

    // forward to router
    if(likely( to_be_sent > 0 )) {
#ifdef BSTGW_VNET_DEBUG
        DMSG("Going to pass %u packets to the router", to_be_sent);
#endif
        if(vnic->net_device.rx_handler(&vnic->net_device, frames, to_be_sent)) {
            EMSG("buffer pass to router failed!");
        }
    }

    free(frames);

#ifdef BSTGW_VNET_DEBUG
    DMSG("Leaving flush_tx() with ret: %d (passed to router: %u)", num_packets, to_be_sent);
#endif
    return num_packets;
}



