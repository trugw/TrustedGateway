#ifndef BSTGW_VNIC_H
#define BSTGW_VNIC_H

#include <internal/vnic-mmio_private.h>
#include <virtio/virtio_net.h>
#include <internal/virtio.h>

#include <internal/virtio-net.h>

#include <notifier.h>
#include <napi.h>
#include <npf_router.h>


typedef struct bstgw_vnic {
    /* VirtIO MMIO */
    mmio_t mmio_conf;
    /* VirtIO Device */
    vdev_t vdev;
    /* VirtIO Net */
    vnet_t vnet;

    struct napi_struct napi; // todo: move to vnet_t?
    struct ifnet net_device; // todo: move somewhere else? (actually vnic is net_device's priv)
} vnic_t;

#endif /* BSTGW_VNIC_H */
