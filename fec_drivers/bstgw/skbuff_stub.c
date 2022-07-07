#include <drivers/bstgw/skbuff_stub.h>

#include <drivers/bstgw/imx_fec.h>

/**
 *  skb_put - add data to a buffer
 *  @skb: buffer to use
 *  @len: amount of data to add
 *
 *  This function extends the used data area of the buffer. If this would
 *  exceed the total buffer size the kernel will panic. A pointer to the
 *  first byte of the extra data is returned.
 */
void *skb_put(struct sk_buff *skb, unsigned int len) {
	const size_t rem_space = skb->eth_buf.buf_cap
		- (skb->eth_buf.data_off + skb->eth_buf.data_len);

	if(unlikely( rem_space < len )) panic("skb_put not enough free buffer space");

	const size_t old_len = skb->eth_buf.data_len;
	skb->eth_buf.data_len += len;
	return bstgw_ethbuf_data_ptr(&skb->eth_buf, old_len);
}

inline struct sk_buff *netdev_alloc_skb(ifnet_t *netdev, unsigned int length)
{
	// TODO: What is the role of the netdev argument? --> for getting CPU pointer of netdev (core-specific skb buffer cache, I guess?)
	(void)netdev;
	assert(length == BSTGW_DEFAULT_BUF_SIZE);
	return (struct sk_buff *)bstgw_ethpool_buf_alloc(NULL);

}