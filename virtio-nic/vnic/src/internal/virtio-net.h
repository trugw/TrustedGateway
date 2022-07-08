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

#ifndef QEMU_VIRTIO_NET_H
#define QEMU_VIRTIO_NET_H

#include <virtio/virtio_net.h>
#include <internal/virtio.h>

#include <dpdk/rte_ether.h>
#include <bstgw_eth_buf.h>

/* previously fixed value */
#define VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE 256
#define VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE 256

#define VIRTIO_NAPI_RETRY (-47)
#define VIRTIO_NO_AVAIL (-48)

// todo
#define TX_BURST 256 // vnic->router
#define RX_BURST 256 // vnic->NW (Linux)

typedef struct virtio_net_conf
{
    uint16_t mtu;
} virtio_net_conf;

struct bstgw_vnetdev;

typedef struct bstgw_vnet_queue {
    struct vqueue *rx_vq;  // device -> Linux  (Linux does receive)
    struct vqueue *tx_vq;  // Linux ->> device (Linux does transmit)

    // TODO: napi/notifier stuff

    // TODO: ref. to tx packet queue?

    struct bstgw_vnetdev *n;
} vnet_q_t;

// TODO: What about struct net_device?
typedef struct bstgw_vnetdev {
    vdev_t *vdev;
    struct rte_ether_addr mac;
    uint16_t status;
    vnet_q_t vqs; // currently only a single pair

    uint32_t tx_burst; // number of packets passed from vnic to router in one call
    uint32_t rx_burst; // *new* number of packets passed from vnic to NW in one call

    virtio_net_conf net_conf;

    size_t config_size;
    uint64_t curr_guest_offloads; // TODO
} vnet_t;


int vnic_net_device_realize(vdev_t *vdev, vnet_t *n);

void virtio_net_set_config(vdev_t *vdev, const uint8_t *config);
void virtio_net_get_config(vdev_t *vdev, uint8_t *config);
void virtio_net_set_status(vdev_t *vdev, uint8_t status);
void virtio_net_reset(vdev_t *vdev);
void virtio_net_set_features(vdev_t *vdev, uint64_t features);

void virtio_net_set_link_status(vnet_t *n, bool link_up);

int32_t virtio_net_flush_tx(vnet_q_t *q);
ssize_t virtio_net_receive(vnet_q_t *q, bstgw_ethbuf_t **ethbufs, unsigned int pktcount);









#endif
