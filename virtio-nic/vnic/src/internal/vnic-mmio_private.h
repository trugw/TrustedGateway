#ifndef BSTGW_VNIC_MMIO_PRIVATE_H
#define BSTGW_VNIC_MMIO_PRIVATE_H

#include <kernel/spinlock.h>
#include <types_ext.h>
#include <stddef.h>
#include <kernel/interrupt.h>

#include <platform_config.h>

#include <secloak/emulation.h>

#include <bstgw/vnic-mmio.h>
#include <qemu/virtio-mmio.h>

#include <internal/virtio.h>

// one rx/tx pair
#define VNIC_QUEUE_MAX        2     // see VIRTIO_QUEUE_MAX
#define VNIC_VQ_MAX_SIZE    256     // see VIRTQUEUE_MAX_SIZE

typedef struct bstgw_mmio {
    vdev_t *vdev;
    
    unsigned int slock;

    paddr_t rg_pa;
    size_t rg_size;
    vaddr_t rg_va;

    struct irq_desc *kick_irq;

    uint32_t host_features_sel;
    uint32_t guest_features_sel;

    uint32_t guest_features[2]; // TODO: why does this exist in proxy and vdev?
    VirtIOMMIOQueue vqs[2];
} mmio_t;

int vnic_kick_nw(mmio_t *vmmio, uint16_t vector __unused);
bool vnic_emu_check(struct region *region, paddr_t address, enum emu_state state, uint32_t *value, int size, bool sign);
#endif /* BSTGW_VNIC_MMIO_PRIVATE_H */