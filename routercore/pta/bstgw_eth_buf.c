#include "bstgw_eth_buf.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <kernel/panic.h>

#include "utils.h"

#include <npf_router.h>

//#define BSTGW_ETHBUF_DEBUG

/* ************************************************************************** */

#include <kernel/spinlock.h>
#include <initcall.h>

struct ethbuf_pool {
	unsigned int slock;
	LIST_HEAD(, bstgw_ether_buffer) free_list;
    LIST_HEAD(, bstgw_ether_buffer) inuse_list;
    unsigned int capacity;
};

static struct ethbuf_pool ether_skb_pool;

static TEE_Result bstgw_ethpool_init(void) {
    ether_skb_pool.slock = SPINLOCK_UNLOCK;
	LIST_INIT(&ether_skb_pool.free_list);
    LIST_INIT(&ether_skb_pool.inuse_list);
    ether_skb_pool.capacity = 0;
    IMSG("ether_skb_pool is ready");
    return TEE_SUCCESS;
}
service_init(bstgw_ethpool_init);

static bstgw_ethbuf_t *
bstgw_ethbuf_alloc_aligned(size_t capacity, size_t priv_size, size_t aligned);
static void bstgw_ethbuf_free(bstgw_ethbuf_t *eb);

void bstgw_ethpool_increase(void *pool __unused, size_t buffer_number) {
    bstgw_ethbuf_t *eb;
    uint32_t exceptions = cpu_spin_lock_xsave(&ether_skb_pool.slock);
    for(unsigned i=0; i<buffer_number; i++) {
        eb = bstgw_ethbuf_alloc_aligned(BSTGW_DEFAULT_BUF_SIZE, BSTGW_PRIV_SIZE, BSTGW_CACHE_LINE);
        if(!eb) panic("OOM @ ethpool_increase()"); // fixme
        LIST_INSERT_HEAD(&ether_skb_pool.free_list, eb, pool_entry);
    }
    ether_skb_pool.capacity += buffer_number;
    cpu_spin_unlock_xrestore(&ether_skb_pool.slock, exceptions);
    //return 0;
}

static inline void bstgw_ethpool_buf_reset(bstgw_ethbuf_t *eb) {
    assert(eb && !eb->next);
    eb->l2_len = 0;
    eb->l3_len = 0;
    eb->data_len = 0;
    eb->data_off = 0;

    npf_mbuf_priv_t *p = (npf_mbuf_priv_t *)eb->priv_data;
    p->flags = 0;

    // todo: too expensive?!
    //memset(eb->priv_data, 0, BSTGW_PRIV_SIZE); // todo: trim to minimum!
}

bstgw_ethbuf_t * bstgw_ethpool_buf_alloc(void *pool __unused) {
    bstgw_ethbuf_t *eb;
    uint32_t exceptions = cpu_spin_lock_xsave(&ether_skb_pool.slock);
    eb = LIST_FIRST(&ether_skb_pool.free_list);
    if(__predict_false( !eb )) panic("poll_buf_alloc OOM");
    LIST_REMOVE(eb, pool_entry);
    LIST_INSERT_HEAD(&ether_skb_pool.inuse_list, eb, pool_entry);
    cpu_spin_unlock_xrestore(&ether_skb_pool.slock, exceptions);
    //bstgw_ethpool_buf_reset(eb); // todo
    return eb;
}

//TODO: eb with next != NULL
void bstgw_ethpool_buf_free(bstgw_ethbuf_t *eb) {
    assert(eb);
    bstgw_ethpool_buf_reset(eb); // todo
    uint32_t exceptions = cpu_spin_lock_xsave(&ether_skb_pool.slock);
    LIST_REMOVE(eb, pool_entry);
    LIST_INSERT_HEAD(&ether_skb_pool.free_list, eb, pool_entry);
    cpu_spin_unlock_xrestore(&ether_skb_pool.slock, exceptions);
}

void bstgw_ethpool_buf_destroy(bstgw_ethbuf_t *eb) {
    assert(eb);
    uint32_t exceptions = cpu_spin_lock_xsave(&ether_skb_pool.slock);
    LIST_REMOVE(eb, pool_entry);
    ether_skb_pool.capacity--;
    cpu_spin_unlock_xrestore(&ether_skb_pool.slock, exceptions);
    bstgw_ethbuf_free(eb);
}


/* ************************************************************************** */

void bstgw_ethbuf_align_check(bstgw_ethbuf_t *eb) {
    assert(eb);
    if(__predict_false( (int)eb->buf & eb->aligned ))
        panic("Failed to properly align ethbuf");
}

static inline void * bstgw_ethpool_alloc_priv(size_t priv_size) {
    /*
     * usually very small, so calloc() should be fine ...
     * but in general it prob. should be malloc() instead
     * with the priv-owner initing it as required
     */
    return calloc(1, priv_size);
}

static bstgw_ethbuf_t *
bstgw_ethbuf_alloc_aligned(size_t capacity, size_t priv_size, size_t align_req)
{
    bool align = (align_req != 0);

    size_t rlen = (!align) ? capacity : roundup2(capacity, align_req) + align_req;
    size_t alloc_len = offsetof(bstgw_ethbuf_t, raw_buf[rlen]);
    bstgw_ethbuf_t *eb = malloc(alloc_len);
    if(__predict_false( !eb )) return NULL;

    // init everything except of data buffer with 0s
    memset(eb, 0, sizeof(bstgw_ethbuf_t));
    eb->buf_cap = capacity;
    eb->aligned = align_req - 1;
    eb->buf = eb->raw_buf;

    // pre-pad for alignment
    if (align) {
        size_t off = ((unsigned long)eb->raw_buf) & eb->aligned;
        if (off) eb->buf = &eb->raw_buf[eb->aligned + 1 - off];
        bstgw_ethbuf_align_check(eb);
    }

    if (__predict_true(priv_size > 0)) {
        eb->priv_data = bstgw_ethpool_alloc_priv(priv_size);
        if (__predict_false(!eb->priv_data)) {
            free(eb);
            return NULL;
        }
    }
    return eb;
}


static void bstgw_ethbuf_free(bstgw_ethbuf_t *eb) {
    if (__predict_false(!eb)) return;
    bstgw_ethbuf_t *next;
    while (eb) {
        next = eb->next;
        if (eb->priv_data) free(eb->priv_data);
        free(eb);
        eb = next;
    }
}


// capacity of the buffer
size_t bstgw_ethbuf_buf_capacity(const bstgw_ethbuf_t *eb) {
    assert(eb);
    return eb->buf_cap;
}

// data/payload length of the buffer
size_t bstgw_ethbuf_data_len(const bstgw_ethbuf_t *eb) {
    assert(eb);
    return eb->data_len;
}

// total length of packet buffer chain
size_t bstgw_ethbuf_total_pkt_len(const bstgw_ethbuf_t *eb) {
    assert(eb);
    // predict true, bcs. we currently do not use chaining, yet
    if (__predict_true(!eb->next)) return eb->data_len;
    size_t len = 0;
    while (eb) {
        len += eb->data_len;
        eb = eb->next;
    }
    return len;
}

void * bstgw_ethbuf_priv(const bstgw_ethbuf_t *eb) {
    assert(eb);
    return eb->priv_data;
}

// pointer to buffer head
uint8_t * bstgw_ethbuf_head(const bstgw_ethbuf_t *eb) {
    assert(eb);
    return (bstgw_ethbuf_t *)eb->buf;
}

// pointer to data[offset]
uint8_t * bstgw_ethbuf_data_ptr(const bstgw_ethbuf_t *eb, size_t offset) {
    assert(eb);
    if (__predict_false( offset >= eb->data_len )) return NULL;
    assert ( (eb->data_off + offset) < eb->buf_cap );
    return &((bstgw_ethbuf_t *)eb)->buf[eb->data_off + offset];
}

// try to linearize the chain
int bstgw_ethbuf_linearize(bstgw_ethbuf_t *eb) {
    if(__predict_false(!eb)) return -1;
    // predict true, bcs. we currently do not use chaining, yet
    if(__predict_true(!eb->next)) return 0;

    // enough free space?
    size_t tail_space = eb->buf_cap - (eb->data_off + eb->data_len);
    size_t rest_buf = bstgw_ethbuf_total_pkt_len(eb) - eb->data_len;
    if(__predict_false( tail_space < rest_buf )) return -1;

    // copy and free loop
    uint8_t *b = bstgw_ethbuf_data_ptr(eb, eb->data_len - 1); // point at last data byte
    b++; // now points just after current data
    
    bstgw_ethbuf_t *next = eb->next;
    while (next) {
        bstgw_ethbuf_t *tmp = next->next;
        memcpy(b, bstgw_ethbuf_data_ptr(next, 0), next->data_len);
        b += next->data_len;
        free(next); // only the eb, not the chain!
        next = tmp;
    }

    eb->data_len += rest_buf;
    eb->next = NULL;
    return 0;
}

/* Warning
 * This function is highly incomplete.
 * Because of alignment issues, this is just used for replacing ethernet headers atm.
 * Also be careful when the buffer length drops to because of that function.
 */
uint8_t * bstgw_ethbuf_precut(bstgw_ethbuf_t *eb, size_t len) {
    if(__predict_false(!eb)) return NULL;
    if(__predict_false(len > eb->data_len)) return NULL;

    eb->data_off += len;
    eb->data_len -= len;
    return &eb->buf[eb->data_off];
}

/* Warning
 * This function is highly incomplete.
 * Because of alignment issues, this is just used for replacing ethernet headers atm.
 */
uint8_t * bstgw_ethbuf_prepend(bstgw_ethbuf_t *eb, size_t len) {
    if(__predict_false(!eb)) return NULL;
    
    size_t head_space = eb->data_off;
    if(__predict_false( head_space < len )) return NULL;

    eb->data_off -= len;
    eb->data_len += len;
    return &eb->buf[eb->data_off];
}

void bstgw_ethbuf_dump_meta(bstgw_ethbuf_t *eb) {
    if(__predict_false(!eb)) return;
    DMSG("Buffer metadata:");
    DMSG("cap: %u, doff: %u, dlen: %u",
        eb->buf_cap, eb->data_off, eb->data_len);
}

void bstgw_ethbuf_dump_data(bstgw_ethbuf_t *eb) {
    if(__predict_false(!eb)) return;
    DMSG("Buffer data dump:");
    while(eb) {
        for (unsigned int i=0; i<eb->data_len; i++) {
            DMSG("0x%x", *bstgw_ethbuf_data_ptr(eb, i));
        }
        eb = eb->next;
    }
}