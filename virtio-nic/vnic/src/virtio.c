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

#include <internal/virtio.h>
#include <internal/vqueue.h>

#include <virtio/virtio_config.h>

// TODO
#include <internal/virtio-net.h>
#include <internal/vnic-mmio_private.h>
#include <internal/vnic.h>

#include <kernel/panic.h>
#include <io.h>

#include <trace.h> // EMSG

//#define BSTGW_VIRTIO_DEBUG

static void virtio_set_isr(vdev_t *vdev, int value) {
    uint8_t old = __atomic_load_n(&vdev->isr, __ATOMIC_RELAXED); // qatomic_read

    /* Do not write ISR if it does not change, so that its cacheline remains
     * shared in the common case where the guest does not read it.
     */
    if ((old & value) != value) {
        __atomic_fetch_or(&vdev->isr, value, __ATOMIC_SEQ_CST); // qatomic_ot()
    }
}

static void virtio_notify_vector(vdev_t *vdev, uint16_t vector) {
    vnic_t *vnic = (vnic_t *)( ((uint8_t *)vdev) - offsetof(vnic_t, vdev));
    vnic_kick_nw(&vnic->mmio_conf, vector);
}

//(TODO: actually there is virtio_notify() with a "should_notify"-check, which then calls virtio_irq())
void virtio_notify(vdev_t *vdev, struct vqueue *vq __unused) {
    // TODO: should check if vq needs notify, but we currently do so before
    virtio_set_isr(vdev, 0x1);
    virtio_notify_vector(vdev, 0);
}

void virtio_update_irq(vdev_t *vdev) {
    virtio_notify_vector(vdev, 0);
}

void virtio_notify_config(vdev_t *vdev) {
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    virtio_set_isr(vdev, 0x3);
    vdev->generation++;
    virtio_notify_vector(vdev, 0);
}

// Note:  I don't see any implementation of that function in QEMU ...
inline int virtio_validate_features(vdev_t *vdev __unused) {
    return 0;
    // Note:  we assume no IOMMU atm; not sure about that feature bit, so let's assume it's not there
    //if (virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM) &&
    //    !virtio_vdev_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM)) {
    //    return -EFAULT;
    //}
}

int virtio_set_status(vdev_t *vdev, uint8_t val) {
    if (!(vdev->status & VIRTIO_CONFIG_S_FEATURES_OK) &&
        val & VIRTIO_CONFIG_S_FEATURES_OK) {
        int ret = virtio_validate_features(vdev);

        if (ret) {
            return ret;
        }
    }

    if ((vdev->status & VIRTIO_CONFIG_S_DRIVER_OK) !=
        (val & VIRTIO_CONFIG_S_DRIVER_OK)) {
        // TODO: might be able to kick that one
        virtio_set_started(vdev, val & VIRTIO_CONFIG_S_DRIVER_OK);
    }

    //k->set_status(vdev, val);
    virtio_net_set_status(vdev, val);

    vdev->status = val;

    return 0;
}

void virtio_reset(vdev_t *vdev) {
    virtio_set_status(vdev, 0);

    //k->reset(vdev);
    virtio_net_reset(vdev);

    vdev->started = false;
//    vdev->broken = false;
    vdev->guest_features = 0;
    vdev->queue_sel = 0;
    vdev->status = 0;
//    vdev->disabled = false;
    __atomic_store_n(&vdev->isr, 0, __ATOMIC_RELAXED); //qatomic_set(&vdev->isr, 0);
    virtio_notify_vector(vdev, 0);

    for(int i = 0; i < VNIC_QUEUE_MAX; i++) {
        vqueue_vring_teardown(&vdev->vq[i]);
        /*vdev->vq[i].vring.desc = 0;
        vdev->vq[i].vring.avail = 0;
        vdev->vq[i].vring.used = 0;
        vdev->vq[i].last_avail_idx = 0;*/

        // TODO: disable napi?

//        vdev->vq[i].used_idx = 0;
//        vdev->vq[i].signalled_used = 0;
//        vdev->vq[i].signalled_used_valid = false;
//        vdev->vq[i].notification = true;
        // TODO:  is that correct? we currently don't do that on init
        // TODO:  also note that QUEUE_NUM, or the set_num call in QUEUE_READY
        //        sets vring.num!
        vdev->vq[i].vring.num = vdev->vq[i].vring.num_default;
//        vdev->vq[i].inuse = 0;
//        virtio_virtqueue_reset_region_cache(&vdev->vq[i]);
    }
}

uint32_t virtio_config_read(vdev_t *vdev, paddr_t offset, int size) {
    if (offset + size > vdev->config_len) {
        EMSG("[vNIC] out-of-region read attempt");
        return 0; //-1 (todo?!)
    }
    
    //k->get_config(vdev, vdev->config);
    virtio_net_get_config(vdev, vdev->config);

    vaddr_t config = (vaddr_t)vdev->config;
    switch (size) {
    case 1:
        return io_read8(config + offset);
    case 2:
        return io_read16(config +offset);
    case 4:
        return io_read32(config + offset);
    default:
        panic("invalid read width");
    }
}

void virtio_config_write(vdev_t *vdev, paddr_t offset, uint32_t value, int size) {
    if (offset + size > vdev->config_len) {
        EMSG("[vNIC] out-of-region write attempt");
        return;
    }
    vaddr_t config = (vaddr_t)vdev->config;
    switch (size) {
        // TODO: replace with memcpy(config + offset, &value, size); instead?
        // (prob. should be fine, bcs. I think we can have the memory cached)
    case 1:
        io_write8(config + offset, value);
        break;
    case 2:
        io_write16(config + offset, value);
        break;
    case 4:
        io_write32(config + offset, value);
        break;
    default:
        panic("invalid write width");
    }
    //k->set_config(vdev, vdev->config);
    virtio_net_set_config(vdev, vdev->config);
}

void virtio_queue_set_rings(vdev_t *vdev, int n, uint64_t desc,
    uint64_t avail, uint64_t used) {

    if (!vdev->vq[n].vring.num) {
        return;
    }

    if ( vqueue_vring_setup(&vdev->vq[n], desc, avail, used) != 0 ) {
        EMSG("CRITICAL: vring setup [#%d] has failed!", n);
        // TODO: virtio-mmio should not enable the queue / indicate an error to the guest!
    }
}

void virtio_queue_set_num(vdev_t *vdev, int n, int num) {
    /* Don't allow guest to flip queue between existent and
     * nonexistent states, or to set it to an invalid size.
     */
    if (!!num != !!vdev->vq[n].vring.num ||
        num > VNIC_VQ_MAX_SIZE ||
        num < 0) {
        return;
    }
    vdev->vq[n].vring.num = num;
}

int virtio_queue_get_num(vdev_t *vdev, int n) {
    return vdev->vq[n].vring.num;
}

void virtio_queue_notify(vdev_t *vdev, int n) {
    struct vqueue *vq = &vdev->vq[n];

    // vring_addr set on set_rings() which is called on QueueReady
//  if (unlikely(!vq->vring.desc)) { // || vdev->broken)) {
    if (unlikely(!vq->vring_addr)) { // || vdev->broken)) {
        EMSG("no notify, bcs. vring not initialized for queue %d", n);
        return;
    }

    vqueue_notify(vq);
}


static int virtio_set_features_nocheck(vdev_t *vdev, uint64_t val)
{
    bool bad = (val & ~(vdev->host_features)) != 0;

    val &= vdev->host_features;
    //    k->set_features(vdev, val);
    virtio_net_set_features(vdev, val);
    vdev->guest_features = val;

    if( vdev->guest_features & VIRTIO_F_VERSION_1 ) {
        IMSG("VIRTIO_F_VERSION_1 negotiated");
    } else {
        panic("Seems that VERSION_1 negotiation has failed!");
    }

    return bad ? -1 : 0;
}

int virtio_set_features(vdev_t *vdev, uint64_t val) {
    /*
     * The driver must not attempt to set features after feature negotiation
     * has finished.
     */
    if (vdev->status & VIRTIO_CONFIG_S_FEATURES_OK) {
        return -EINVAL;
    }
    return virtio_set_features_nocheck(vdev, val);
}
