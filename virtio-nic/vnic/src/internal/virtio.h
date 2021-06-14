/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_H
#define QEMU_VIRTIO_H

#include <stdint.h>
#include <types_ext.h> // paddr_t
#include <assert.h>

struct vqueue;

typedef struct bstgw_vdev {
    /* VirtIO Device */
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    uint64_t guest_features;
    uint64_t host_features;
    size_t config_len;
    void *config; // struct virtio_net_config
    uint32_t generation;
    struct vqueue *vq;
    uint16_t device_id;
    bool use_started; // TODO: might be able to kick that, bcs. we currently only support non-legacy
    bool started; // TODO: we might even be able to kick that one, bcs. I don't see much use of it in QEMU
} vdev_t;

static inline void virtio_add_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features |= (1ULL << fbit);
}

static inline void virtio_clear_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features &= ~(1ULL << fbit);
}

static inline bool virtio_has_feature(uint64_t features, unsigned int fbit)
{
    assert(fbit < 64);
    return !!(features & (1ULL << fbit));
}

static inline bool virtio_vdev_has_feature(vdev_t *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->guest_features, fbit);
}

static inline bool virtio_host_has_feature(vdev_t *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->host_features, fbit);
}

static inline void virtio_set_started(vdev_t *vdev, bool started) {
    vdev->started = started;
}

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0) 
#endif

#ifndef likely
#define likely(x)        __builtin_expect(!!(x), 1)
#endif

void virtio_update_irq(vdev_t *vdev);
void virtio_notify_config(vdev_t *vdev);
void virtio_notify(vdev_t *vdev, struct vqueue *vq);

int virtio_validate_features(vdev_t *vdev);
int virtio_set_status(vdev_t *vdev, uint8_t val);
void virtio_reset(vdev_t *vdev);
int virtio_queue_get_num(vdev_t *vdev, int n);
void virtio_queue_set_num(vdev_t *vdev, int n, int num);
int virtio_set_features(vdev_t *vdev, uint64_t val);
uint32_t virtio_config_read(vdev_t *vnic, paddr_t offset, int size);
void virtio_config_write(vdev_t *vnic, paddr_t offset, uint32_t value, int size);
void virtio_queue_set_rings(vdev_t *vdev, int n, uint64_t desc,
    uint64_t avail, uint64_t used);
void virtio_queue_notify(vdev_t *vdev, int n);

#endif