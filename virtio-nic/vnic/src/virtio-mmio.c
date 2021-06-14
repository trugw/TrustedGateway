/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <internal/vnic.h>
#include <internal/vqueue.h>
#include <internal/virtio.h>

#include <bstgw/vnic-mmio.h>

#include <virtio/virtio_mmio.h>
#include <virtio/virtio_config.h>

#include <kernel/interrupt.h>

#include <kernel/panic.h>
#include <assert.h>

//#define BSTGW_VIRTIO_MMIO_DBG

int vnic_kick_nw(mmio_t *vmmio, uint16_t vector __unused) {
    assert(vmmio != NULL && vmmio->vdev != NULL);
    if (vmmio->vdev && vmmio->vdev->isr != 0) {
#ifdef BSTGW_VIRTIO_MMIO_DBG
        DMSG("Going to kick the Normal World using an IRQ.");
#endif
        return irq_raise(vmmio->kick_irq);
    } else {
#ifdef BSTGW_VIRTIO_MMIO_DBG
        DMSG("Not kicking the NW, bcs. ISR is 0"); // todo: avoid call to kick on isr==0
#endif
        return 0;
    }
}

static void virtio_mmio_soft_reset(mmio_t *vmmio) {
    for (int i = 0; i < VNIC_QUEUE_MAX; i++) {
        vmmio->vqs[i].enabled = 0;
    }
}

// TODO:  for write + read:  thread-safety?!
// TODO:  replace SEC_IO memory with SEC rw memory? (bcs. I don't think we have to drop caching here?
//          -- but not sure, depends on auto vs manual inter-core cache coherency)

static void vnic_handle_mmio_write(mmio_t *vmmio, paddr_t offset, uint32_t value, int size) {
#ifdef BSTGW_VIRTIO_MMIO_DBG
    DMSG("VNIC mmio write: offset:= 0x%04lx, value:= 0x%08x", offset, value);
#endif

    assert(vmmio != NULL && vmmio->vdev != NULL);
    vdev_t *vdev = vmmio->vdev;

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        virtio_config_write(vdev, offset, value, size);
        return;
    }

    if (size != 4) {
        EMSG("[vNIC] wrong size write access to mmio register");
        return;
    }
    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        if (value) {
            vmmio->host_features_sel = 1;
        } else {
            vmmio->host_features_sel = 0;
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        vmmio->guest_features[vmmio->guest_features_sel] = value;
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        if (value) {
            vmmio->guest_features_sel = 1;
        } else {
            vmmio->guest_features_sel = 0;
        }
        break;
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
        EMSG("write to legacy register in non-legacy mode (guest_page_size)");
        return;
    case VIRTIO_MMIO_QUEUE_SEL:
        if (value < VNIC_QUEUE_MAX) {
            vdev->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        virtio_queue_set_num(vdev, vdev->queue_sel, value); // note: also called in QUEUE_READY
        vmmio->vqs[vdev->queue_sel].num = value;
        break;
    case VIRTIO_MMIO_QUEUE_ALIGN:
        EMSG("write to legacy register in non-legacy mode (queue_align)");
        return;
    case VIRTIO_MMIO_QUEUE_PFN:
        EMSG("write to legacy register in non-legacy mode (queue_pfn)");
        return;
    case VIRTIO_MMIO_QUEUE_READY:
        if (value) {
            virtio_queue_set_num(vdev, vdev->queue_sel,
                                 vmmio->vqs[vdev->queue_sel].num);

            // TODO: how to detect and handle error?!
            virtio_queue_set_rings(vdev, vdev->queue_sel,

                ((uint64_t)vmmio->vqs[vdev->queue_sel].desc[1]) << 32 |
                vmmio->vqs[vdev->queue_sel].desc[0],

                ((uint64_t)vmmio->vqs[vdev->queue_sel].avail[1]) << 32 |
                vmmio->vqs[vdev->queue_sel].avail[0],

                ((uint64_t)vmmio->vqs[vdev->queue_sel].used[1]) << 32 |
                vmmio->vqs[vdev->queue_sel].used[0]);

            // TODO: how to avoid on prev. error?
            vmmio->vqs[vdev->queue_sel].enabled = 1;
        } else {
            vmmio->vqs[vdev->queue_sel].enabled = 0;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < VNIC_QUEUE_MAX) {
#ifdef BSTGW_VIRTIO_MMIO_DBG
            DMSG("mmio:  queue notify");
#endif
            virtio_queue_notify(vdev, value);
        }
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
#ifdef BSTGW_VIRTIO_MMIO_DBG
        DMSG("isr before:  %d", vdev->isr);
#endif
        __atomic_fetch_and(&vdev->isr, ~value, __ATOMIC_SEQ_CST); // qatomic_and()
#ifdef BSTGW_VIRTIO_MMIO_DBG
        DMSG("isr afterwards:  %d", vdev->isr);
#endif
        virtio_update_irq(vdev); // TODO: why? (will immediately interrupt the NW again ...)
        break;
    case VIRTIO_MMIO_STATUS:
        if (!(value & VIRTIO_CONFIG_S_DRIVER_OK)) {
            // TODO: what to do here?:  stop NAPI?
            //virtio_mmio_stop_ioeventfd(vmmio);
            EMSG("WARNING:  virtio_mmio_stop_ioeventfd(); missing");
        }

        if ((value & VIRTIO_CONFIG_S_FEATURES_OK)) {
            virtio_set_features(vdev,
                                ((uint64_t)vmmio->guest_features[1]) << 32 |
                                vmmio->guest_features[0]);
        }

        virtio_set_status(vdev, value & 0xff);

        if (value & VIRTIO_CONFIG_S_DRIVER_OK) {
            // TODO: what to do here?
            //virtio_mmio_start_ioeventfd(vmmio);
#ifdef BSTGW_VIRTIO_MMIO_DBG
            DMSG("virtio_mmio_start_ioeventfd();");
#endif
        }

        if (vdev->status == 0) {
            virtio_reset(vdev);
            virtio_mmio_soft_reset(vmmio);
        }
        break;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        vmmio->vqs[vdev->queue_sel].desc[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        vmmio->vqs[vdev->queue_sel].desc[1] = value;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        vmmio->vqs[vdev->queue_sel].avail[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        vmmio->vqs[vdev->queue_sel].avail[1] = value;
        break;
    case VIRTIO_MMIO_QUEUE_USED_LOW:
        vmmio->vqs[vdev->queue_sel].used[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        vmmio->vqs[vdev->queue_sel].used[1] = value;
        break;
    case VIRTIO_MMIO_MAGIC_VALUE:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICE_ID:
    case VIRTIO_MMIO_VENDOR_ID:
    case VIRTIO_MMIO_DEVICE_FEATURES:
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
    case VIRTIO_MMIO_INTERRUPT_STATUS:
    case VIRTIO_MMIO_CONFIG_GENERATION:
        EMSG("[vNIC] write to read-only register: %lu", offset);
        break;

    default:
        EMSG("[vNIC] invalid register offset: %lu", offset);
    }
}

static uint32_t vnic_handle_mmio_read(mmio_t *vmmio, paddr_t offset, int size) {
//TODO: 32/64_t: static uint64_t virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
#ifdef BSTGW_VIRTIO_MMIO_DBG
    DMSG("VNIC mmio read: offset:= 0x%04lx, size:= %d", offset, size);
#endif

    assert(vmmio != NULL && vmmio->vdev != NULL);
    vdev_t *vdev = vmmio->vdev;

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        return virtio_config_read(vdev, offset, size);
    }

    if (size != 4) {
        EMSG("[vNIC] wrong size access to mmio register");
        return 0;
    }
    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICE_ID:
        return vdev->device_id;
    case VIRTIO_MMIO_VENDOR_ID:
        return VIRT_VENDOR;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        return (vdev->host_features) //& ~vnic->legacy_features)
            >> (32 * vmmio->host_features_sel);
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        // TODO: we currently seem to return 0, shouldn't we fix that or is it fine bcs. not yet initialized?
        if (!virtio_queue_get_num(vdev, vdev->queue_sel)) {
            return 0;
        }
        return VNIC_VQ_MAX_SIZE;
    case VIRTIO_MMIO_QUEUE_PFN:
        EMSG("[vNIC] read from legacy register QUEUE_PFN in non-legacy mode");
        return 0;
    case VIRTIO_MMIO_QUEUE_READY:
        return vmmio->vqs[vdev->queue_sel].enabled;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return __atomic_load_n(&vdev->isr, __ATOMIC_RELAXED); // qatomic_read
    case VIRTIO_MMIO_STATUS:
        return vdev->status;
    case VIRTIO_MMIO_CONFIG_GENERATION:
        return vdev->generation;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
    case VIRTIO_MMIO_DRIVER_FEATURES:
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
    case VIRTIO_MMIO_QUEUE_SEL:
    case VIRTIO_MMIO_QUEUE_NUM:
    case VIRTIO_MMIO_QUEUE_ALIGN:
    case VIRTIO_MMIO_QUEUE_NOTIFY:
    case VIRTIO_MMIO_INTERRUPT_ACK:
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        EMSG("[vNIC] read of write-only register: %lu", offset);
        return 0;

    default:
        EMSG("[vNIC] invalid register offset: %lu", offset);
        return 0;
    }

    return 0;
}

bool vnic_emu_check(struct region *region, paddr_t address, enum emu_state state, uint32_t *value, int size, bool sign) {
    bool allowed = true;
    switch (state) {
        case EMU_STATE_WRITE:
            assert(value != NULL);
            vnic_handle_mmio_write(region->user_data, address - region->base, *value, size);
            allowed = false;
            break;

	    case EMU_STATE_READ_BEFORE:
            break;

	    case EMU_STATE_READ_AFTER:
            if (value == NULL) panic("value is NULL in read_after");
            uint32_t read_val;
            switch(size) {
                case 1:
                    read_val = (uint8_t)vnic_handle_mmio_read(region->user_data, address - region->base, size);
                    break;
                case 2:
                    read_val = (uint16_t)vnic_handle_mmio_read(region->user_data, address - region->base, size);
                    break;
                case 4:
                    read_val = vnic_handle_mmio_read(region->user_data, address - region->base, size);
                    break;
                default:
                    panic("Invalid read width");
            }

            if (sign) {
                // TODO: copied from secloak/emulation.c
                if (size == 1) {
                    if ((read_val) & 0x80) {
                        read_val |= 0xFFFFFF00;
                    }
                } else if (size == 2) {
                    if ((read_val) & 0x8000) {
                        read_val |= 0xFFFF0000;
                    }
                }
                // --
            }
            *value = read_val;
            break;

        default:
            panic("unknown emulation state");
    }
    return allowed;
}
