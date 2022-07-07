#ifndef __BSTGW_PKTQ_H
#define __BSTGW_PKTQ_H

#include "bstgw_eth_buf.h"

#include "utils.h"

#include <assert.h>

#include <stdatomic.h>

typedef enum {
    BSTGW_PKTQ_STOPPED = 0, // default on initialization
    BSTGW_PKTQ_STARTED,
} pktq_state_t;

typedef struct {
	unsigned int	    slock;
//    pktq_state_t        state;
    atomic_bool         stopped;

    unsigned int        next_read;
    unsigned int        next_write;

	unsigned int        count;
    unsigned int        capacity;
	//bstgw_ethbuf_t *	pkt[];
    bstgw_ethbuf_t      **pkt;
} pktqueue_t;

/* NOTE: all APIs named "*_locked_*" require external locking using &pq.slock */

static inline bool bstgw_pktq_locked_is_full(pktqueue_t *pq) {
    assert(pq);
    return (pq->count >= pq->capacity);
}

static inline bool bstgw_pktq_locked_is_empty(pktqueue_t *pq) {
    assert(pq);
    return (pq->count == 0);
}

static inline size_t bstgw_pktq_locked_bulk_add_buff(pktqueue_t *pq, bstgw_ethbuf_t **bs, size_t nbufs) {
    assert(pq && bs);
    int space_left = pq->capacity - pq->count;
    int will_add = (nbufs <= space_left) ? nbufs : space_left;
    for(int i = 0; i < will_add; i++) {
        pq->pkt[pq->next_write] = bs[i];
        pq->next_write++;
        pq->next_write %= pq->capacity;
    }
    pq->count += will_add;
    return will_add;
}

static inline bool bstgw_pktq_locked_add_buf(pktqueue_t *pq, bstgw_ethbuf_t *b) {
    assert(pq && b);
    if(__predict_false( bstgw_pktq_locked_is_full(pq) )) return false;
    pq->pkt[pq->next_write] = b;
    pq->next_write++;
    pq->next_write %= pq->capacity;
    pq->count++;
    return true;
}

static inline bool bstgw_pktq_locked_drop_bufs(pktqueue_t *pq, size_t num) {
    assert(pq && (pq->count >= num));
    //if(__predict_false( pq->count < num )) return false;
    pq->next_read += num;
    pq->next_read %= pq->capacity;
    pq->count -= num;
    return true;
}

/* Number of contigous buffers.
 * Assuming return value is n, that means, that the interval
 * pkt[next_read], pkt[next-read+(n-1)] can be consumed without a wrap-around.
 * Note that more packets might be in the queue, but which require a wrap-around
 * during reading.
 */
static inline size_t bstgw_pktq_locked_contig_bufs(pktqueue_t *pq) {
    assert(pq);
    if(__predict_false( bstgw_pktq_locked_is_empty(pq) )) return 0;
    size_t till_end = 1 + ((pq->capacity - 1) - pq->next_read);
    if (till_end > pq->count) return pq->count;
    return till_end;
}

#endif /* __BSTGW_PKTQ_H */