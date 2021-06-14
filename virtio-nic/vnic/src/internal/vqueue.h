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

#ifndef _LIB_TRUSTY_VQUEUE_H
#define _LIB_TRUSTY_VQUEUE_H

/* Note: trusty api version 3, bcs. 5 already uses shared mem. ids and we don't
 *		 want to struggle with OP-TEE's shared mem. API for this PoC */


#include <kernel/spinlock.h>
#include <kernel/mutex.h>
//#include <kernel/event.h>
#include <mm/core_memprot.h>
//#include <lib/extmem/extmem.h>
#include <internal/uio.h>
#include <stdint.h>
#include <string.h>
//#include <sys/types.h>

#include <virtio/virtio_ring.h>

//#define BSTGW_VQUEUE_HDR_DBG

struct vqueue;
typedef int (*vqueue_cb_t)(struct vqueue *vq, void *priv);

struct vqueue {
	uint32_t		id;

	struct vring	vring;
	vaddr_t			vring_addr;
	size_t			vring_sz;
	unsigned long   vring_align;

	unsigned int	slock;

	uint16_t		last_avail_idx;

	struct condvar *avail_event;
	struct mutex *mtx_avail_event;
	bool own_event; // free condvar/mutex on destory

	/* called when the vq is kicked *from* the other side */
	vqueue_cb_t		notify_cb;

	/* called when we want to kick the other side */
	vqueue_cb_t		kick_cb;

	void			*priv;
};

struct vqueue_iovs {
	unsigned int	cnt;   /* max number of iovs available */
	unsigned int	used;  /* number of iovs currently in use */
	size_t			len;   /* total length of all used iovs */
	paddr_t			*phys; /* PA of each iov (map_iov() maps them and stores VAs at iovs->base) */
	iovec_kern_t	*iovs;
};

struct vqueue_buf {
	uint16_t		head;
	uint16_t		padding;
	struct vqueue_iovs	in_iovs;
	struct vqueue_iovs	out_iovs;
};

int vqueue_init(struct vqueue *vq, uint32_t id, unsigned int num, unsigned long align,
		void *priv, vqueue_cb_t notify_cb, vqueue_cb_t kick_cb,
		struct condvar *avail_cv, struct mutex *avail_mtx);
void vqueue_destroy(struct vqueue *vq);

int vqueue_vring_setup(struct vqueue *vq, paddr_t desc_pa, paddr_t avail_pa, paddr_t used_pa);
void vqueue_vring_teardown(struct vqueue *vq);

uint16_t vqueue_num_of_avail_buf_and_notify(struct vqueue *vq);
uint16_t vqueue_num_of_avail_buf(struct vqueue *vq);
int vqueue_get_avail_buf(struct vqueue *vq, struct vqueue_buf *iovbuf);

int vqueue_map_iovs(struct vqueue_iovs *vqiovs, unsigned int flags);
void vqueue_unmap_iovs(struct vqueue_iovs *vqiovs);

int vqueue_add_buf(struct vqueue *vq, struct vqueue_buf *buf, uint32_t len);

/*void vqueue_signal_avail(struct vqueue *vq);*/
void vqueue_napi_signal_avail(struct vqueue *vq /*, struct napi_struct *n */);

static inline uint32_t vqueue_id(struct vqueue *vq)
{
	return vq->id;
}

static inline int vqueue_notify(struct vqueue *vq)
{
#ifdef BSTGW_VQUEUE_HDR_DBG
	DMSG("vqueue_notify");
#endif
	if (vq->notify_cb) {
#ifdef BSTGW_VQUEUE_HDR_DBG
		DMSG("calling vq->notify_cb");
#endif
		return vq->notify_cb(vq, vq->priv);
	}
	return 0;
}

static inline int vqueue_kick(struct vqueue *vq)
{
	if (vq->kick_cb)
		return vq->kick_cb(vq, vq->priv);
	return 0;
}

#endif /* _LIB_TRUSTY_VQUEUE_H */
