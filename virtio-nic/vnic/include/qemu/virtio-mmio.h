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

#ifndef HW_VIRTIO_MMIO_H
#define HW_VIRTIO_MMIO_H

#define VIRT_MAGIC 0x74726976 /* 'virt' */
#define VIRT_VERSION 2
#define VIRT_VERSION_LEGACY 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */

typedef struct VirtIOMMIOQueue {
    uint16_t num;
    bool enabled;
    uint32_t desc[2];
    uint32_t avail[2];
    uint32_t used[2];
} VirtIOMMIOQueue;
#endif
