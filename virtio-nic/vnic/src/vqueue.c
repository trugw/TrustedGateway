/*
 * Copyright (c) 2013-2014, Google, Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <internal/err.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>

#include <internal/uio.h>

#include <virtio/virtio_ring.h>
#include <internal/vqueue.h>

#include <kernel/panic.h>

// TODO: arch-specific!
#include <arm32.h>

#include <mm/core_memprot.h>

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0) 
#endif


int vqueue_init(struct vqueue *vq, uint32_t id, unsigned int num, unsigned long align,
		void *priv, vqueue_cb_t notify_cb, vqueue_cb_t kick_cb,
		struct condvar *avail_cv, struct mutex *avail_mtx) {

	assert(vq);
	assert(avail_cv);
	assert(avail_mtx);

	//vq->vring_sz = vring_size(num, align);
	vq->vring_align = align;
	memset( &vq->vring, 0, sizeof(struct vring) );
	vq->vring.num = num;
	vq->vring.num_default = num;

	vq->vring.desc = 0;
	vq->vring.avail = 0;
	vq->vring.used = 0;

	vq->last_avail_idx = 0;

	vq->id = id;
	vq->priv = priv;
	vq->notify_cb = notify_cb;
	vq->kick_cb = kick_cb;

	vq->slock = SPINLOCK_UNLOCK;
	vq->vring_addr = 0;

	vq->avail_event = avail_cv;
	vq->mtx_avail_event = avail_mtx;

	return 0;
}

void vqueue_destroy(struct vqueue *vq) {
	assert(vq);
	vqueue_vring_teardown(vq);
	if (vq->own_event) {
		condvar_destroy(vq->avail_event); free(vq->avail_event);
		mutex_destroy(vq->mtx_avail_event); free(vq->mtx_avail_event);
	}
}

// todo: until we know or fix the layout
static int check_ring_layout(paddr_t pa, vaddr_t ref_va) {
	int ret = -1;
	vaddr_t va = core_mmu_get_va(pa, MEM_AREA_RAM_NSEC);
	if (!va) {
		EMSG("Warning:  given PA is NOT MAPPED");
	} else {
		if ( va == ref_va ) {
#ifdef BSTGW_VQUEUE_HDR_DBG
			DMSG("Perfect, the VAs match!");
#endif
			ret = 0;
		} else {
			EMSG("Warning:  the VAs are different!");
			EMSG("expected: %lu, got: %lu", ref_va, va);
		}
	}
	return ret;
}

int vqueue_vring_setup(struct vqueue *vq, paddr_t desc_pa, paddr_t avail_pa, paddr_t used_pa) {
	vaddr_t vaddr = 0;

	assert(vq);
	assert(desc_pa);
	assert(avail_pa);
	assert(used_pa);

	vq->vring_sz = vring_size(vq->vring.num, vq->vring_align); // todo: fine here?

	// TODO: MEM_AREA_NSEC_SHM
	vaddr = core_mmu_get_va(desc_pa, MEM_AREA_RAM_NSEC);
	if (!vaddr) {
		IMSG("Warning: vring not yet mapped! Trying to map it.");
		if (core_mmu_add_mapping(MEM_AREA_RAM_NSEC, desc_pa, ROUNDUP(vq->vring_sz, SMALL_PAGE_SIZE)))
			vaddr = core_mmu_get_va(desc_pa, MEM_AREA_RAM_NSEC);
	}
	if (!vaddr) {
		EMSG("cannot map vring at %lu with size %u", desc_pa, ROUNDUP(vq->vring_sz, SMALL_PAGE_SIZE)); 
		return -1;
	}

	vq->vring_addr = vaddr;
	vq->vring.desc = (void *)vaddr;

	/* TODO:  Option 2 - map the other 2 and just ignore the default layout */
	unsigned int num = vq->vring.num;
	unsigned long align = vq->vring_align;

	// cf. Linux's virtio_ring.h
	vq->vring.avail = (void*)((uint8_t *)vaddr + num*sizeof(struct vring_desc));
	vq->vring.used = (void *)(((uintptr_t)&vq->vring.avail->ring[num] + sizeof(__virtio16)
					+ align-1) & ~(align - 1));

	// TODO: is the given layout the VA or PA layout?
#ifdef BSTGW_VQUEUE_HDR_DBG
	DMSG("Check avail address");
#endif
	if(check_ring_layout(avail_pa, (vaddr_t)vq->vring.avail)) goto error;
#ifdef BSTGW_VQUEUE_HDR_DBG
	DMSG("Check used address");
#endif
	if(check_ring_layout(used_pa, (vaddr_t)vq->vring.used)) goto error;

	return 0;

error:
	// todo!
	vq->vring_addr = 0;
	vq->vring.desc = NULL;
	vq->vring.avail = NULL;
	vq->vring.used = NULL;
	return -1;
}

void vqueue_vring_teardown(struct vqueue *vq) {
	uint32_t exceptions;
	vaddr_t vring_addr;
	assert(vq);

	exceptions = cpu_spin_lock_xsave(&vq->slock);
	vring_addr = vq->vring_addr;
	vq->vring_addr = (vaddr_t)NULL;
	vq->vring_sz = 0;
	vq->last_avail_idx = 0;
	cpu_spin_unlock_xrestore(&vq->slock, exceptions);

	if (vring_addr) {
		vq->vring.used = NULL;
		vq->vring.avail = NULL;

		// TODO: unmap if dynamically mapped

		vq->vring.desc = NULL;
	} else {
		assert(!vq->vring.desc);
		assert(!vq->vring.avail);
		assert(!vq->vring.used);
	}
}



/*void vqueue_signal_avail(struct vqueue *vq)
{
	uint32_t exceptions;

	mutex_lock(vq->mtx_avail_event);

	exceptions = cpu_spin_lock_xsave(&vq->slock);
	if (vq->vring_addr)
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;	
	
	condvar_signal(vq->avail_event);

	cpu_spin_unlock_xrestore(&vq->slock, exceptions);

	mutex_unlock(vq->mtx_avail_event);
}*/

void vqueue_napi_signal_avail(struct vqueue *vq /*, struct napi_struct *n */)
{
	uint32_t exceptions;

	exceptions = cpu_spin_lock_xsave(&vq->slock);

	if (vq->vring_addr) {
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
		dsb(); //smp_rmb();
#ifdef BSTGW_VQUEUE_HDR_DBG
		DMSG("set USED-NO_NOTIFY flag");
#endif
	}

	cpu_spin_unlock_xrestore(&vq->slock, exceptions);
	/*napi_schedule(n);*/ // TODO: should it be in vq->slock part?
}




/* The other side of virtio pushes buffers into our avail ring, and pulls them
 * off our used ring. We do the reverse. We take buffers off the avail ring,
 * and put them onto the used ring.
 */

static int _vqueue_get_avail_buf_locked(struct vqueue *vq,
					struct vqueue_buf *iovbuf)
{
	uint16_t next_idx;
	struct vring_desc *desc;

	assert(vq);
	assert(iovbuf);

	if (!vq->vring_addr) {
		/* there is no vring - return an error */
		return ERR_CHANNEL_CLOSED;
	}

	/* the idx counter is free running, so check that it's no more
	 * than the ring size away from last time we checked... this
	 * should *never* happen, but we should be careful. */
	uint16_t avail_cnt = vq->vring.avail->idx - vq->last_avail_idx;
	if (unlikely(avail_cnt > (uint16_t) vq->vring.num)) {
		/* such state is not recoverable */
		EMSG("vq %p: new avail idx out of range (old %u new %u)",
				vq, vq->last_avail_idx, vq->vring.avail->idx);
		panic("vq new avail idx out of range");
	}

	if (vq->last_avail_idx == vq->vring.avail->idx) {
		/* no buffers left */
		return ERR_NOT_ENOUGH_BUFFER;
#if 0
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;
		dsb(); // TODO: do { dsb(); outer_sync(); } while(0); //smp_mb();
		if (vq->last_avail_idx == vq->vring.avail->idx) {
			/* no buffers left */
			return ERR_NOT_ENOUGH_BUFFER;
		}
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
#endif
	}
	dsb(); //smp_rmb();

	next_idx = vq->vring.avail->ring[vq->last_avail_idx % vq->vring.num];
	vq->last_avail_idx++;

	if (unlikely(next_idx >= vq->vring.num)) {
		/* index of the first descriptor in chain is out of range.
		   vring is in non recoverable state: we cannot even return
		   an error to the other side */
		EMSG("vq %p: head out of range %u (max %u)",
		       vq, next_idx, vq->vring.num);
		panic("vq head out of range");
	}

	iovbuf->head = next_idx;
	iovbuf->in_iovs.used = 0;
	iovbuf->in_iovs.len  = 0;
	iovbuf->out_iovs.used = 0;
	iovbuf->out_iovs.len  = 0;

	do {
		struct vqueue_iovs *iovlist;

		if (unlikely(next_idx >= vq->vring.num)) {
			/* Descriptor chain is in invalid state.
			 * Abort message handling, return an error to the
			 * other side and let it deal with it.
			 */
			EMSG("vq %p: head out of range %u (max %u)\n",
			       vq, next_idx, vq->vring.num);
			return ERR_NOT_VALID;
		}

		desc = &vq->vring.desc[next_idx];
		if (desc->flags & VRING_DESC_F_WRITE)
			iovlist = &iovbuf->out_iovs;
		else
			iovlist = &iovbuf->in_iovs;

		if (iovlist->used < iovlist->cnt) {
			/* .base will be set when we map this iov */
			iovlist->iovs[iovlist->used].len = desc->len;
			iovlist->phys[iovlist->used] = (paddr_t) desc->addr;
			assert(iovlist->phys[iovlist->used] == desc->addr);
			iovlist->used++;
			iovlist->len += desc->len;
		} else {
			EMSG("Warning: NW using too many iovecs -- %u (we allowed %u)",
				iovlist->used, iovlist->cnt);
			return ERR_TOO_BIG;
		}

		/* go to the next entry in the descriptor chain */
		next_idx = desc->next;
	} while (desc->flags & VRING_DESC_F_NEXT);

	return NO_ERROR;
}

int vqueue_get_avail_buf(struct vqueue *vq, struct vqueue_buf *iovbuf)
{
	uint32_t exceptions;

	exceptions = cpu_spin_lock_xsave(&vq->slock);
	int ret = _vqueue_get_avail_buf_locked(vq, iovbuf);
	cpu_spin_unlock_xrestore(&vq->slock, exceptions);
	return ret;
}


uint16_t vqueue_num_of_avail_buf(struct vqueue *vq) {
	return vq->vring.avail->idx - vq->last_avail_idx;
}


uint16_t vqueue_num_of_avail_buf_and_notify(struct vqueue *vq) {
	// todo: not sure if that is accurate as they don't use avail_cnt further
	// also: ping only keeps decreasing it; on later load we actually see re-increases
	uint16_t num_bufs = vq->vring.avail->idx - vq->last_avail_idx;
    if (!num_bufs) {
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;
		dsb(); // TODO: do { dsb(); outer_sync(); } while(0); //smp_mb();
		/* check for race */
		if (vq->last_avail_idx == vq->vring.avail->idx) {
			/* no buffers left */
			return 0;
		}
		num_bufs = vq->vring.avail->idx - vq->last_avail_idx;
    }
    vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
    dsb(); //smp_rmb();  || TODO: is that expensive?
    return num_bufs;
}



// TODO
int vqueue_map_iovs(struct vqueue_iovs *vqiovs, unsigned int flags __unused)
{
	unsigned int i;
	int ret;

	assert(vqiovs);
	assert(vqiovs->phys);
	assert(vqiovs->iovs);
	assert(vqiovs->used <= vqiovs->cnt);

	for (i = 0; i < vqiovs->used; i++) {
        vqiovs->iovs[i].base = NULL;

		// TODO: MEM_AREA_NSEC_SHM
		// TODO: access rights depending on flags parameter
		vaddr_t vaddr = core_mmu_get_va(vqiovs->phys[i], MEM_AREA_RAM_NSEC);
		vqiovs->iovs[i].base = (void *)vaddr;
		if (!vaddr)
			goto err;
/*
		ret = vmm_alloc_physical(vmm_get_kernel_aspace(), "vqueue",
		                         ROUNDUP(vqiovs->iovs[i].len, SMALL_PAGE_SIZE),
		                         &vqiovs->iovs[i].base, SMALL_PAGE_SHIFT,
		                         vqiovs->phys[i], 0, flags);
		if (ret)
			goto err;
*/
	}

	return NO_ERROR;

err:
	while (i--) {
		// TODO: do anything? (we don't have easy unmap + need to keep stuf mapped for SeCloak atm)
		//vmm_free_region(vmm_get_kernel_aspace(),
		//                (vaddr_t)vqiovs->iovs[i].base);
		vqiovs->iovs[i].base = NULL;
	}
	return ret;
}

// TODO
void vqueue_unmap_iovs(struct vqueue_iovs *vqiovs)
{
	assert(vqiovs);
	assert(vqiovs->phys);
	assert(vqiovs->iovs);
	assert(vqiovs->used <= vqiovs->cnt);

	for (unsigned int i = 0; i < vqiovs->used; i++) {
		/* base is expected to be set */
		assert(vqiovs->iovs[i].base);
		// TODO: do anything? (we don't have easy unmap + need to keep stuf mapped for SeCloak atm)
//		vmm_free_region(vmm_get_kernel_aspace(),
//		                (vaddr_t)vqiovs->iovs[i].base);
		vqiovs->iovs[i].base = NULL;
	}
}

static int _vqueue_add_buf_locked(struct vqueue *vq, struct vqueue_buf *buf, uint32_t len)
{
	struct vring_used_elem *used;

	assert(vq);
	assert(buf);

	if (!vq->vring_addr) {
		/* there is no vring - return an error */
		return ERR_CHANNEL_CLOSED;
	}

	if (buf->head >= vq->vring.num) {
		/* this would probable mean corrupted vring */
		EMSG("vq %p: head (%u) out of range (%u)\n",
		         vq, buf->head, vq->vring.num);
		return ERR_NOT_VALID;
	}

	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = buf->head;
	used->len = len;
	dsb_ishst(); // TODO:  do { dsb(st); outer_sync(); } while (0) // smp_wmb();
	vq->vring.used->idx++;
	return NO_ERROR;
}

int vqueue_add_buf(struct vqueue *vq, struct vqueue_buf *buf, uint32_t len)
{
	uint32_t exceptions;

	exceptions = cpu_spin_lock_xsave(&vq->slock);
	int ret = _vqueue_add_buf_locked(vq, buf, len);
	cpu_spin_unlock_xrestore(&vq->slock, exceptions);
	return ret;
}
