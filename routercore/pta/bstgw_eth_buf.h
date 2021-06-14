#ifndef __BSTGW_ETH_BUF_H
#define __BSTGW_ETH_BUF_H

// from dpdk
#define RTE_PTYPE_L3_IPV4                   0x00000010
#define RTE_PTYPE_L3_IPV6                   0x00000040
#define  RTE_ETH_IS_IPV4_HDR(ptype) ((ptype) & RTE_PTYPE_L3_IPV4)
#define  RTE_ETH_IS_IPV6_HDR(ptype) ((ptype) & RTE_PTYPE_L3_IPV6)

#include <stddef.h>
#include <stdint.h>

#include <sys/queue.h>

#include <platform_config.h>
#define BSTGW_CACHE_LINE (STACK_ALIGNMENT) // (==64Bytes) note: 32 might be correct for the imx6

// dpdk: rte_mbuf_ptype.h
#define RTE_PTYPE_L2_MASK                   0x0000000f

#define BSTGW_DEFAULT_BUF_SIZE (2048) // == FEC_ENET_RX_FRSIZE
#define BSTGW_PRIV_SIZE (sizeof(npf_mbuf_priv_t))

/*
 * Packet buffer for holding an Ethernet frame (2B aligned data!)
 * multiple buffers can be chained (todo: add API)
 */
struct bstgw_ether_buffer;
struct bstgw_ether_buffer {
    LIST_ENTRY(bstgw_ether_buffer) pool_entry;
    struct bstgw_ether_buffer *next;

    unsigned int ol_flags;
    uint32_t packet_type;

    unsigned short l2_len; // link layer (ethernet)
    unsigned short l3_len; // network layer (ip)

	/* TODO: tx checksum offloading */
	uint8_t ip_summed:2; // TODO: merge into "ol_flags" (offload flags)
	struct {
		uint16_t csum_start;
		uint16_t csum_offset;
	};

    void *priv_data;

    size_t data_len;
    size_t data_off;

    size_t buf_cap;
    uint8_t *buf; // pointer into raw_buf, i.e., sub-array of it (*buf)[]

    size_t aligned;
    uint8_t raw_buf[];
};
typedef struct bstgw_ether_buffer bstgw_ethbuf_t;




// capacity of the buffer
inline size_t bstgw_ethbuf_buf_capacity(const bstgw_ethbuf_t *);

// data/payload length of the buffer
inline size_t bstgw_ethbuf_data_len(const bstgw_ethbuf_t *);

// total length of packet buffer chain
size_t bstgw_ethbuf_total_pkt_len(const bstgw_ethbuf_t *);

inline void * bstgw_ethbuf_priv(const bstgw_ethbuf_t *);

// pointer to buffer head
inline uint8_t * bstgw_ethbuf_head(const bstgw_ethbuf_t *);

// pointer to data[offset]
inline uint8_t * bstgw_ethbuf_data_ptr(const bstgw_ethbuf_t *, size_t);

inline void bstgw_ethbuf_align_check(bstgw_ethbuf_t *);

// try to linearize the chain
int bstgw_ethbuf_linearize(bstgw_ethbuf_t *);

/* bstgw_ethbuf_precut
 * Remove space from start of data.
 * Returns pointer to new data start.
 *
 * Warning
 * This function is highly incomplete.
 * Because of alignment issues, this is just used for replacing ethernet headers atm.
 * Also be careful when the buffer length drops to because of that function.
 */
// cut from start of data
// returns pointer to new start of data
inline uint8_t * bstgw_ethbuf_precut(bstgw_ethbuf_t *, size_t);

/* bstgw_ethbuf_prepend
 * Add new space to start of data.
 * Returns pointer to new data start.
 * 
 * Warning
 * This function is highly incomplete.
 * Because of alignment issues, this is just used for replacing ethernet headers atm.
 */
inline uint8_t * bstgw_ethbuf_prepend(bstgw_ethbuf_t *, size_t);

// TODO: add to/remove from chain

void bstgw_ethbuf_dump_meta(bstgw_ethbuf_t *);
void bstgw_ethbuf_dump_data(bstgw_ethbuf_t *);

/* ************************************************************************** */

/* Add #buffer_number buffers to the pool */
/*int*/ void bstgw_ethpool_increase(void *, size_t buffer_number);

/* Allocate an ethernet buffer from the pool */
bstgw_ethbuf_t * bstgw_ethpool_buf_alloc(void *);

/* Put ethernet buffer back into the pool */
void bstgw_ethpool_buf_free(bstgw_ethbuf_t *eb);

/* Destroy a buffer, i.e., remove it from the pool and free its memory */
void bstgw_ethpool_buf_destroy(bstgw_ethbuf_t *eb);

#endif /* __BSTGW_ETH_BUF_H */
