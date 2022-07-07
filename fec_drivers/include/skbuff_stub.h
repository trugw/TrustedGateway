#ifndef BSTGW_SKBUFF_STUB_H
#define BSTGW_SKBUFF_STUB_H

#include "utils.h"
#include <npf_router.h>

#include <newlib/netinet/ip.h>

#include <bstgw_eth_buf.h>
struct sk_buff {
	struct bstgw_ether_buffer eth_buf;
};

void *skb_put(struct sk_buff *skb, unsigned int len);

static inline void dev_kfree_skb(struct sk_buff *skb) {
	bstgw_ethpool_buf_free((bstgw_ethbuf_t *)skb);
}

// note: actually this is for calling for diff. reasons / from
// different contexts (e.g., process, irq, unknown)
static inline void dev_kfree_skb_any(struct sk_buff *skb) {
	dev_kfree_skb(skb);
}

// headlen is the linear data length, i.e., our data_len;
// our skb->len analogue is bstgw_ethbuf_total_pkt_len();
static inline unsigned int skb_headlen(struct sk_buff *skb) {
	return skb->eth_buf.data_len;
}

static inline struct ip *ip_hdr(const struct sk_buff *skb) {
    return (struct ip *) bstgw_ethbuf_data_ptr(
        &skb->eth_buf, skb->eth_buf.l2_len
    );
}

static inline unsigned char *skb_data(struct sk_buff *skb) {
	assert(skb);
	return &skb->eth_buf.buf[skb->eth_buf.data_off];

    // note: caused NULL return when data_len was 0 (directly after allocation)
	//return bstgw_ethbuf_data_ptr(&skb->eth_buf, 0);
}

static inline const unsigned char *skb_head(struct sk_buff *skb) {
	return bstgw_ethbuf_head(&skb->eth_buf);
}

/* TODO: scatter gather I/O support */
static inline unsigned char skb_nr_frags(struct sk_buff *skb) {
	if(likely( !skb->eth_buf.next )) return 0;
	// count number of chained ethbufs
	unsigned char n = 0;
	bstgw_ethbuf_t *b = skb->eth_buf.next;
	while( b ) { n++; b = b->next; }
	return n;
}

// macros to allow r/w access
#define skb_ip_summed(sb) 	sb->eth_buf.ip_summed
#define skb_csum_start(sb) 	sb->eth_buf.csum_start
#define skb_csum_offset(sb) sb->eth_buf.csum_offset

/**
 *  skb_reserve - adjust headroom
 *  @skb: buffer to alter
 *  @len: bytes to move
 *
 *  Increase the headroom of an empty &sk_buff by reducing the tail
 *  room. This is only allowed for an empty buffer.
 */
// TODO: we don't have an explicit head/tail-room definition;
//		 closest would be to move the data_offset while data_len is 0,
//		 s.t., the data would not start at 0;
//		NOTE:  often this fct. is used for the 2B ethernet-IP alginment fix,
//			   but we already have that in our struct definition atm.
static inline void skb_reserve(struct sk_buff *skb, int len) {
	assert(skb->eth_buf.data_len == 0);
	int cap = bstgw_ethbuf_buf_capacity(&skb->eth_buf);
	if(unlikely( len <= (cap - skb->eth_buf.data_off) )) {
		EMSG("reserve len > rest buffer size (len: %d, data_off: %u)", len, skb->eth_buf.data_off);
		panic();
	}
	// TODO: alignment breaking risk! (adjust FEC driver)
	skb->eth_buf.data_off += len;
}

/*TODO: "Although this function allocates memory it can be called
 *      from an interrupt."
 * __netdev_alloc_skb(dev, length, GFP_ATOMIC);
 */
struct sk_buff *netdev_alloc_skb(ifnet_t *netdev, unsigned int length);

/**
 *  skb_pull - remove data from the start of a buffer
 *  @skb: buffer to use
 *  @len: amount of data to remove
 *
 *  This function removes data from the start of a buffer, returning
 *  the memory to the headroom. A pointer to the next data in the buffer
 *  is returned. Once the data has been pulled future pushes will overwrite
 *  the old data.
 */
static inline void *skb_pull_inline(struct sk_buff *skb, unsigned int len) {
	return bstgw_ethbuf_precut(&skb->eth_buf, len);
}

static inline void *skb_pull(struct sk_buff *skb, unsigned int len) {
    return skb_pull_inline(skb, len);
}

static inline
void skb_checksum_none_assert(const struct sk_buff *skb __maybe_unused) {
	// TODO: assert() still not working again
	assert(skb->ip_summed == CHECKSUM_NONE);
}

#endif /* BSTGW_SKBUFF_STUB_H */