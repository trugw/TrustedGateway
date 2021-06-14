#ifndef _UAPI_LINUX_VIRTIO_NET_H
#define _UAPI_LINUX_VIRTIO_NET_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#include <virtio/virtio_config.h>

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM	0	/* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM	1	/* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 /* Dynamic offload configuration. */
#define VIRTIO_NET_F_MTU	3	/* Initial MTU advice */
#define VIRTIO_NET_F_MAC	5	/* Host has given MAC address. */
#define VIRTIO_NET_F_GUEST_TSO4	7	/* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6	8	/* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN	9	/* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_GUEST_UFO	10	/* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4	11	/* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6	12	/* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN	13	/* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO	14	/* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF	15	/* Host can merge receive buffers. */
#define VIRTIO_NET_F_STATUS	16	/* virtio_net_config.status available */
#define VIRTIO_NET_F_CTRL_VQ	17	/* Control channel available */
#define VIRTIO_NET_F_CTRL_RX	18	/* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN	19	/* Control channel VLAN filtering */
#define VIRTIO_NET_F_CTRL_RX_EXTRA 20	/* Extra RX mode control support */
#define VIRTIO_NET_F_GUEST_ANNOUNCE 21	/* Guest can announce device on the network */
#define VIRTIO_NET_F_MQ	22	/* Device supports Receive Flow Steering */
#define VIRTIO_NET_F_CTRL_MAC_ADDR 23	/* Set MAC address */

#ifndef VIRTIO_NET_NO_LEGACY
#define VIRTIO_NET_F_GSO	6	/* Host handles pkts w/ any GSO type */
#endif /* VIRTIO_NET_NO_LEGACY */

#define VIRTIO_NET_S_LINK_UP	1	/* Link is up */
#define VIRTIO_NET_S_ANNOUNCE	2	/* Announcement is needed */

#include <dpdk/rte_ether.h>

struct virtio_net_config {
	/* The config defining mac address (if VIRTIO_NET_F_MAC) */
	uint8_t mac[RTE_ETHER_ADDR_LEN];
	/* See VIRTIO_NET_F_STATUS and VIRTIO_NET_S_* above */
	uint16_t status;
	/* Maximum number of each of transmit and receive queues;
	 * see VIRTIO_NET_F_MQ and VIRTIO_NET_CTRL_MQ.
	 * Legal values are between 1 and 0x8000
	 */
	uint16_t max_virtqueue_pairs;
	/* Default maximum transmit unit advice */
	uint16_t mtu;
} __attribute__((packed)); // TODO: is packed problematic?


/*
 * This header comes first in the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header.
 *
 * This is bitwise-equivalent to the legacy struct virtio_net_hdr_mrg_rxbuf,
 * only flattened.
 */
struct virtio_net_hdr_v1 {
#define VIRTIO_NET_HDR_F_NEEDS_CSUM	1	/* Use csum_start, csum_offset */
#define VIRTIO_NET_HDR_F_DATA_VALID	2	/* Csum is valid */
	uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE		0	/* Not a GSO frame */
#define VIRTIO_NET_HDR_GSO_TCPV4	1	/* GSO frame, IPv4 TCP (TSO) */
#define VIRTIO_NET_HDR_GSO_UDP		3	/* GSO frame, IPv4 UDP (UFO) */
#define VIRTIO_NET_HDR_GSO_TCPV6	4	/* GSO frame, IPv6 TCP */
#define VIRTIO_NET_HDR_GSO_ECN		0x80	/* TCP has ECN set */
	uint8_t gso_type;
	uint16_t hdr_len;	/* Ethernet + IP + tcp/udp hdrs */
	uint16_t gso_size;	/* Bytes to append to hdr_len per frame */
	uint16_t csum_start;	/* Position to start checksumming from */
	uint16_t csum_offset;	/* Offset after that to place checksum */
	uint16_t num_buffers;	/* Number of merged rx buffers */
};


#endif /* _UAPI_LINUX_VIRTIO_NET_H */
