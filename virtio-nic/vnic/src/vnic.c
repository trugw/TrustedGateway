#include <platform_config.h>
#include <mm/core_memprot.h>
#include <initcall.h>

#include <kernel/panic.h>

#include <string.h>

#include <drivers/dt.h>

#include <assert.h>

#include <internal/vnic.h>
#include <internal/vqueue.h>
#include <internal/virtio.h>

#include <kernel/mutex.h>

#include <virtio/virtio_config.h> // VIRTIO_F_VERSION_1

#include <npf_router.h> // TODO!

#include <bstgw_netif_utils.h>

//#define BSTGW_VNIC_DEBUG

#define vnic_rx_vq(x) (x->vdev.vq[0])
#define vnic_tx_vq(x) (x->vdev.vq[1])

register_phys_mem(MEM_AREA_IO_SEC, BSTGW_VNIC_START, BSTGW_VNIC_SIZE);

static vnic_t *bstgw_vnic;

static struct irq_chip *vnic_get_gic_irq_chip(void) {
    void *fdt;
	if (!(fdt = phys_to_virt(CFG_DT_ADDR, MEM_AREA_RAM_NSEC))) {
		EMSG("[vNIC] Failed to get DT reference");
        return NULL;
	}

/* TODO: use this already exposed API instead
    fdt32_t gic_phandle;
    struct device *chip_dev = dt_lookup_device(fdt, gic_phandle);
*/

    // TODO: make this more generic (e.g., search for gic in compatible properties)
    //       or depending on VNIC node go VNIC path up or read its intc handle
    int gic_offset = fdt_path_offset(fdt, "/soc/interrupt-controller@00a01000");
	if (gic_offset < 0) {
		EMSG("[vNIC] Failed to find GIC DT node");
        return NULL;
	}

    struct device *chip_dev = device_lookup(gic_offset);
    if (chip_dev == NULL) {
        EMSG("[vNIC] Looking up GIC device failed");
        return NULL;
    }


    return irq_find_chip(chip_dev);
}

/* current guest sets 0x0000 0001 for upper 32 bits --> == VIRTIO_F_VERSION_1
 * [[for lower 32 bits --> 0x000108ab]]
 * for lower 32 bits --> 0x0001002b
 * 
 * bit 16           --> STATUS
 * [[bit 11           --> HOST_TSO4]]
 * bit 5 [[+ 7]]        --> MAC [[+ GUEST_TSO4]]
 * bit 0 + 1 + 3    --> CSUM + GUEST_CUSM + MTU
 * 
 * ==> accepted all of our features
 */
static void vnic_vdev_init_feature_bits(vdev_t *vdev) {
    assert(vdev != NULL);
    vdev->host_features = 0;

    // non-legacy device
    virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);

    // VirtIO-Net features
//TODO: implement + re-enable CSUM support
//    virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_CSUM); // calculate for (Linux -> Ext.) packets
//    virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_GUEST_CSUM); // validate csum for (Linux <<- Ext.) packets
    virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_MTU);
    virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_MAC);
    //virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_GUEST_TSO4);
    //virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_HOST_TSO4);
    virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_STATUS);

    // this is ARP-related; not sure atm
    // virtio_add_feature(&vdev->host_features, VIRTIO_NET_F_GUEST_ANNOUNCE);
    // TODO: depends on VIRTIO_NET_F_CTRL_VQ
}

static void vnic_vdev_init(vdev_t *vdev) {
    vdev->status = 0;
    vdev->isr = 0;
    vdev->queue_sel = 0;
    vdev->guest_features = 0;

    vdev->generation = 0; // TODO: is 0 valid, or does it have to be >0 ?
    vdev->device_id = 0x1; // network card
    vdev->use_started = true; // TODO: might kick that field completely, bcs. I think all non-legacy use it
    vdev->started = false;
}

static struct virtio_net_config *vnic_init_config(void) {
    struct virtio_net_config *net_conf = calloc(1, sizeof(struct virtio_net_config));
    if (net_conf == NULL) return NULL;

    // TODO: set to actual NIC mac? or sth. else? [depends on our final setup]
    net_conf->mac[0] = 0x00;
    net_conf->mac[1] = 0x19;
    net_conf->mac[2] = 0xb8;
    net_conf->mac[3] = 0x08;
    net_conf->mac[4] = 0x40;
    net_conf->mac[5] = 0x78;

    //TODO: instead trigger from somewhere else using virtio_net_set_link_status(vnet, true)
    //net_conf->status = 0;
    net_conf->status = VIRTIO_NET_S_LINK_UP;

    net_conf->max_virtqueue_pairs = 1; // we will not support that anyway (NET_F_MQ)
    net_conf->mtu = 1500; // only for testing; want to increase it later; (should it be 1518 instead?!)

    return net_conf;
}

static TEE_Result vnic_init_vmmio(mmio_t *vmmio) {
    assert(vmmio != NULL);
    vmmio->slock = SPINLOCK_UNLOCK;

    struct irq_desc *it = malloc(sizeof(struct irq_desc));
    // TODO: consider integration into DT / device init code instead
    if (!it || !(it->chip = vnic_get_gic_irq_chip())) {
        if (it) {
            EMSG("[vNIC] Failed to get GIC IRQ chip");
            free(it);
        }
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    it->irq = 32 + BSTGW_VNIC_IRQ; // +32: GIC shift for SPIs (cf. irq_map)

    vmmio->kick_irq = it;
    vmmio->host_features_sel = 0;
    // todo

    return TEE_SUCCESS;
}

static void vnic_vnet_init(vnet_t *vnet) {
    assert(vnet);
    vnet->curr_guest_offloads = 0;
    vnet->tx_burst = TX_BURST; // TODO
    vnet->rx_burst = RX_BURST; // TODO
}


static int vnic_vq_notify_cb(struct vqueue *vq, void *priv) {
    vnic_t *vnic = priv;

    vqueue_napi_signal_avail(vq /*, &vnic->napi*/);
    // TODO: the vqueues share NAPI, so should also unmask/signal the other one
    //       to avoid unneccessary double DAs, which is super expensive!
    napi_schedule(&vnic->napi); // todo: might move it into napi_signal
    return 0;
}

static int vnic_vq_kick_cb(struct vqueue *vq, void *priv) {
    vnic_t *vnic = priv;
    // TODO: correct?
    if (!vq->vring.avail->flags & VRING_AVAIL_F_NO_INTERRUPT) {
        virtio_notify(&vnic->vdev, vq);
    } else {
    }
    return 0;
}



static int vnic_poll(struct napi_struct *napi, int budget) {
    assert(napi);
    int pkts = 0;

    vnic_t *vnic = netdev_priv(napi->net_dev);
    assert(vnic);

    /* we are scheduled, so no need for DAs atm. (would be better if directly
     * on napi_schedule call; but therefore would need a CB to abstract away
     * napi_schedule calls where we could add this functionality) */
    vnic->vnet.vqs.rx_vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
    vnic->vnet.vqs.tx_vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
    dsb(); //smp_rmb();

    do {
        int ret = 0;
        bool events = false;

        // NW -> VNIC -> Router
        ret = virtio_net_flush_tx(&vnic->vnet.vqs);
        if(ret>0) pkts += ret;
        events |= ((ret > 0) || (ret == VIRTIO_NAPI_RETRY));
#ifdef BSTGW_VNIC_DEBUG
        if(unlikely( (ret < 0) && (ret != VIRTIO_NAPI_RETRY) )) {
            EMSG("an error has occurred while receiving data from the NW (ret: %d)", ret);
            //TODO: return ret;
        }
#endif

        ret = router_pktq_tx(napi->net_dev);
        events |= ((ret > 0) || (ret == VIRTIO_NAPI_RETRY));
#ifdef BSTGW_VNIC_DEBUG
        if(unlikely( (ret < 0) && (ret != VIRTIO_NAPI_RETRY) )) {
            EMSG("an error has occurred during VNIC receive (ret: %d)", ret);
            // todo
        }
#endif

//VIRTIO_NO_AVAIL

        if (!events) {
            // todo: Shouldn't napi_complete() return value affect NOTIFY on/off?
            if( !napi_complete(napi) ) {
            } else {
                // TODO: here all NW interrupts would need to be re-armed (or check to be already)

                // Check if new packets from NW to pass to router, or not
                if(vqueue_num_of_avail_buf_and_notify(vnic->vnet.vqs.tx_vq)) {
                    napi_schedule(napi);
                    // already rescheduled, no need to unmask tx queue
                    goto vnic_fast_out;
                }

                /* Only unmask if we wanted to send sth., but there were no
                 * buffers available. Else we will get either kicked by new
                 * packets from NW (rx vqueue) or by router passing packets for
                 * us to pass to NW (tx vqueue).
                 *
                 * ret would be 0 if we didn't even try to send sth.
                 */
                if(ret == VIRTIO_NO_AVAIL) {
                    if(vqueue_num_of_avail_buf_and_notify(vnic->vnet.vqs.rx_vq))
                    {
                        napi_schedule(napi);
                        // TODO: also can mask RX queue then again, as we anyway will be scheduled again!
                    }  
                }
            }
vnic_fast_out:
            return pkts;
        }
    } while(pkts < budget);

    return pkts;
}


// there're new enqueued egress packets
// TODO: implement
static int vnic_egr_ntfy_handler(struct ifnet *net_dev) {
    vnic_t *vnic = netdev_priv(net_dev);
    //vnic_vq_notify_cb(&vnic_rx_vq(vnic), vnic); // wrong, bcs. means sth. else
    napi_schedule(&vnic->napi);
    return 0;
}


// -> vnic -> nw
static int vnic_xmit_handler(struct ifnet *net_dev, bstgw_ethbuf_t **pkts /* *pkts[] */, unsigned int pktcount) {
    vnic_t *vnic = netdev_priv(net_dev);

    // TODO: should be handled via netif_tx_queue_stopped() in router instead
    // means: not initialized/ready yet!
    if(unlikely( !vnic_tx_vq(vnic).vring_addr )) return 0;

#ifdef BSTGW_VNIC_DEBUG
    DMSG("vnic_xmit_handler( npkts:= %u ) got called", pktcount);
#endif
    return virtio_net_receive(&vnic->vnet.vqs, pkts, pktcount);
}






static TEE_Result vnic_init(void) {
    // Check region size
    size_t conf_rg_size = BSTGW_VNIC_SIZE;
    if (conf_rg_size == 0) {
        EMSG("vnic config region is too small");
        return TEE_ERROR_SHORT_BUFFER;
    }

    // Get config base
    paddr_t conf_rg_pa = BSTGW_VNIC_START;
    vaddr_t conf_rg_va = core_mmu_get_va(conf_rg_pa, MEM_AREA_IO_SEC);
    if (!conf_rg_va) {
        EMSG("vnic config region not mapped");
        return TEE_ERROR_GENERIC;
    }

    memset((void *)conf_rg_va, 0, conf_rg_size);
#ifdef BSTGW_VNIC_DEBUG
    DMSG("[vNIC] vnic config region: (pa: 0x%x, va: 0x%lx)", BSTGW_VNIC_START, conf_rg_va);
#endif

    bstgw_vnic = malloc(sizeof(vnic_t));
    if (!bstgw_vnic) return TEE_ERROR_OUT_OF_MEMORY;

    TEE_Result res = TEE_SUCCESS;

    /* Virtio-MMIO */
    memset(&bstgw_vnic->mmio_conf, 0, sizeof(bstgw_vnic->mmio_conf));
    if( (res = vnic_init_vmmio(&bstgw_vnic->mmio_conf)) != TEE_SUCCESS ) {
        goto vnic_err1;
    }
    bstgw_vnic->mmio_conf.rg_size = conf_rg_size;
    bstgw_vnic->mmio_conf.rg_pa = conf_rg_pa;
    bstgw_vnic->mmio_conf.rg_va = conf_rg_va;

    /* Virtio-Device */
    vnic_vdev_init(&bstgw_vnic->vdev);
    vnic_vdev_init_feature_bits(&bstgw_vnic->vdev);

    // "attach" to virtio-mmio
    bstgw_vnic->mmio_conf.vdev = &bstgw_vnic->vdev;

    /* Virtio-net (TODO!) */
    memset(&bstgw_vnic->vnet, 0, sizeof(vnet_t));
    vnic_vnet_init(&bstgw_vnic->vnet);
    bstgw_vnic->vnet.vdev = &bstgw_vnic->vdev; // convenience

    // TODO
    bstgw_vnic->vnet.config_size = sizeof(struct virtio_net_config);
    struct virtio_net_config *net_conf;
    if( (net_conf = vnic_init_config()) == NULL ) {
        EMSG("Failed to init virtio-net config");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err2;
    }
    bstgw_vnic->vnet.status = net_conf->status;
    //bstgw_vnic->vnet.max_queues = 1; // NOTE: refers to 1 NetQueue == 1 VQ PAIR (rx,tx)
    bstgw_vnic->vnet.net_conf.mtu = net_conf->mtu;
    /* Setting the VNIC MAC exposed to the Normal World */
    memcpy(bstgw_vnic->vnet.mac.addr_bytes, net_conf->mac, RTE_ETHER_ADDR_LEN);

//TODO: populate the ethernet buffer pool
    //bstgw_ethpool_increase(NULL, bstgw_vnic->vnet.tx_burst);


    // vdev copy
    bstgw_vnic->vdev.config_len = bstgw_vnic->vnet.config_size;
    bstgw_vnic->vdev.config = net_conf;

    // VirtQueue Pair
    if ( (bstgw_vnic->vdev.vq = calloc(2, sizeof(struct vqueue))) == NULL ) {
        EMSG("Failed to allocate vnic rx and tx queue");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err3;
    }

    // shared condvar and mutex
    struct condvar *cv = calloc(1, sizeof(struct condvar));
    struct mutex *mtx = calloc(1, sizeof(struct mutex));
    if (!cv || !mtx) {
        EMSG("Failed initialization of vqueue pair");
        res = TEE_ERROR_OUT_OF_MEMORY;   
        goto vnic_err4;
    }
    condvar_init(cv);
    mutex_init(mtx);

    // init rx vq (sw -> nw)
    if (vqueue_init(&vnic_rx_vq(bstgw_vnic), 0, VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE, SMALL_PAGE_SIZE,
    bstgw_vnic, vnic_vq_notify_cb, vnic_vq_kick_cb, cv, mtx) != 0) {
        EMSG("Failed initing rx vqueue");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err5;
    }

    // init tx vq (nw ->> sw)
    if (vqueue_init(&vnic_tx_vq(bstgw_vnic), 1, VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE, SMALL_PAGE_SIZE,
    bstgw_vnic, vnic_vq_notify_cb, vnic_vq_kick_cb, cv, mtx) != 0) {
        EMSG("Failed initing tx vqueue");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err6;
    }

    // add refs to vnet
    bstgw_vnic->vnet.vqs.rx_vq = &vnic_rx_vq(bstgw_vnic);
    bstgw_vnic->vnet.vqs.tx_vq = &vnic_tx_vq(bstgw_vnic);
    bstgw_vnic->vnet.vqs.n = &bstgw_vnic->vnet;


    /* NetDevice + NAPI */

    if (ifnet_dev_init(&bstgw_vnic->net_device) != 0) {
        EMSG("net_device initialization failed");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err7;
    }
    bstgw_vnic->net_device.priv_data = bstgw_vnic;
    /* Setting the trusted router-internal IP of the NW interface */
    //rte_ether_addr_copy(&bstgw_vnic->vnet.mac, &bstgw_vnic->net_device.hwaddr;
    bstgw_vnic->net_device.hwaddr.addr_bytes[0] = 0xfa;
    bstgw_vnic->net_device.hwaddr.addr_bytes[1] = 0xfa;
    bstgw_vnic->net_device.hwaddr.addr_bytes[2] = 0xde;
    bstgw_vnic->net_device.hwaddr.addr_bytes[3] = 0xad;
    bstgw_vnic->net_device.hwaddr.addr_bytes[4] = 0xbe;
    bstgw_vnic->net_device.hwaddr.addr_bytes[5] = 0xef;


    const char *vnic_name = "NW";
    strncpy(bstgw_vnic->net_device.name, vnic_name, IF_NAMESIZE);

    bstgw_vnic->net_device.trust_status = BSTGW_IF_NONSEC;

    bstgw_vnic->net_device.rx_handler = bstgw_generic_rx_handler;
    bstgw_vnic->net_device.xmit_handler = vnic_xmit_handler;
    bstgw_vnic->net_device.egress_ntfy_handler = vnic_egr_ntfy_handler;

    /* TODO: VNIC does not yet set start/stop correctly on queue inits
     *       instead it currently check initialization in the xmit handler.
     */
    //bstgw_vnic->net_device.egress_queue->state = BSTGW_PKTQ_STARTED;
    netif_tx_start_queue(bstgw_vnic->net_device.egress_queue);

    // napi
    napi_instance_setup(&bstgw_vnic->napi, cv, mtx);

    // TODO: weight?
    netif_napi_add(&bstgw_vnic->net_device, &bstgw_vnic->napi, vnic_poll /*poll*/, 65 /*weight*/);
    // TODO: add this and disable add adquate positions
    napi_enable(&bstgw_vnic->napi);

    if( ifnet_dev_register(&bstgw_vnic->net_device) != 0 ) {
        EMSG("ifnet_dev_register() failed");
        res = TEE_ERROR_GENERIC;
        goto vnic_err8;
    }


    /* enable r/w traps for config region */
    if (emu_add_region(conf_rg_pa, conf_rg_size, vnic_emu_check, &bstgw_vnic->mmio_conf) != 0) {
        EMSG("Failed adding emulation region");
        res = TEE_ERROR_OUT_OF_MEMORY;
        goto vnic_err9;
    }

    IMSG("[vNIC] vnic is ready");
    return res;

    //
vnic_err9:
    //TODO:
    // ifnet_dev_unregister(&bstgw_vnic->net_device);
vnic_err8:
    // TODO:
    // napi_unregister(&bstgw_vnic->napi);
    // ifnet_dev_fini(&bstgw_vnic->net_device);
vnic_err7:
    vqueue_destroy(&vnic_tx_vq(bstgw_vnic));
vnic_err6:
    vqueue_destroy(&vnic_rx_vq(bstgw_vnic));
vnic_err5:
    mutex_destroy(mtx);
    condvar_destroy(cv);
vnic_err4:
    if(cv) free(cv);
    if(mtx) free(mtx);
    free(bstgw_vnic->vdev.vq);
vnic_err3:
    free(bstgw_vnic->vdev.config);
vnic_err2:
    free(bstgw_vnic->mmio_conf.kick_irq);
vnic_err1:
    //bstgw_vnic->mmio_conf.vdev = NULL;
    free(bstgw_vnic);
    bstgw_vnic = NULL;
    if(res == TEE_SUCCESS) { panic("Unexpected vNIC initialization error"); }
    return res;
}

static TEE_Result bstgw_vnic_init(void) {
    return vnic_init();
}

//driver_init_late(bstgw_vnic_init);
driver_init(bstgw_vnic_init);
