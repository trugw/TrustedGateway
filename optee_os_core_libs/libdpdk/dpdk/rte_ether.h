/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 */

#ifndef _RTE_ETHER_H_
#define _RTE_ETHER_H_

/**
 * @file
 *
 * Ethernet Helpers in RTE
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define RTE_ETHER_ADDR_LEN  6 /**< Length of Ethernet address. */
#define RTE_ETHER_TYPE_LEN  2 /**< Length of Ethernet type field. */
#define RTE_ETHER_CRC_LEN   4 /**< Length of Ethernet CRC. */
#define RTE_ETHER_HDR_LEN   \
	(RTE_ETHER_ADDR_LEN * 2 + \
		RTE_ETHER_TYPE_LEN) /**< Length of Ethernet header. */
#define RTE_ETHER_MIN_LEN   64    /**< Minimum frame len, including CRC. */
#define RTE_ETHER_MAX_LEN   1518  /**< Maximum frame len, including CRC. */
#define RTE_ETHER_MTU       \
	(RTE_ETHER_MAX_LEN - RTE_ETHER_HDR_LEN - \
		RTE_ETHER_CRC_LEN) /**< Ethernet MTU. */

#define RTE_ETHER_MAX_VLAN_FRAME_LEN \
	(RTE_ETHER_MAX_LEN + 4)
	/**< Maximum VLAN frame length, including CRC. */

#define RTE_ETHER_MAX_JUMBO_FRAME_LEN \
	0x3F00 /**< Maximum Jumbo frame length, including CRC. */

#define RTE_ETHER_MAX_VLAN_ID  4095 /**< Maximum VLAN ID. */

#define RTE_ETHER_MIN_MTU 68 /**< Minimum MTU for IPv4 packets, see RFC 791. */

/**
 * Ethernet address:
 * A universally administered address is uniquely assigned to a device by its
 * manufacturer. The first three octets (in transmission order) contain the
 * Organizationally Unique Identifier (OUI). The following three (MAC-48 and
 * EUI-48) octets are assigned by that organization with the only constraint
 * of uniqueness.
 * A locally administered address is assigned to a device by a network
 * administrator and does not contain OUIs.
 * See http://standards.ieee.org/regauth/groupmac/tutorial.html
 */
struct rte_ether_addr {
	uint8_t addr_bytes[RTE_ETHER_ADDR_LEN]; /**< Addr bytes in tx order */
} __attribute__ ((aligned(2)));

#define RTE_ETHER_LOCAL_ADMIN_ADDR 0x02 /**< Locally assigned Eth. address. */
#define RTE_ETHER_GROUP_ADDR  0x01 /**< Multicast or broadcast Eth. address. */

/**
 * Check if two Ethernet addresses are the same.
 *
 * @param ea1
 *  A pointer to the first ether_addr structure containing
 *  the ethernet address.
 * @param ea2
 *  A pointer to the second ether_addr structure containing
 *  the ethernet address.
 *
 * @return
 *  True  (1) if the given two ethernet address are the same;
 *  False (0) otherwise.
 */
static inline int rte_is_same_ether_addr(const struct rte_ether_addr *ea1,
				     const struct rte_ether_addr *ea2)
{
	const uint16_t *w1 = (const uint16_t *)ea1;
	const uint16_t *w2 = (const uint16_t *)ea2;

	return ((w1[0] ^ w2[0]) | (w1[1] ^ w2[1]) | (w1[2] ^ w2[2])) == 0;
}

/**
 * Check if an Ethernet address is filled with zeros.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is filled with zeros;
 *   false (0) otherwise.
 */
static inline int rte_is_zero_ether_addr(const struct rte_ether_addr *ea)
{
	const uint16_t *w = (const uint16_t *)ea;

	return (w[0] | w[1] | w[2]) == 0;
}

/**
 * Check if an Ethernet address is a unicast address.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is a unicast address;
 *   false (0) otherwise.
 */
static inline int rte_is_unicast_ether_addr(const struct rte_ether_addr *ea)
{
	return (ea->addr_bytes[0] & RTE_ETHER_GROUP_ADDR) == 0;
}

/**
 * Check if an Ethernet address is a multicast address.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is a multicast address;
 *   false (0) otherwise.
 */
static inline int rte_is_multicast_ether_addr(const struct rte_ether_addr *ea)
{
	return ea->addr_bytes[0] & RTE_ETHER_GROUP_ADDR;
}

/**
 * Check if an Ethernet address is a broadcast address.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is a broadcast address;
 *   false (0) otherwise.
 */
static inline int rte_is_broadcast_ether_addr(const struct rte_ether_addr *ea)
{
	const uint16_t *ea_words = (const uint16_t *)ea;

	return (ea_words[0] == 0xFFFF && ea_words[1] == 0xFFFF &&
		ea_words[2] == 0xFFFF);
}

/**
 * Check if an Ethernet address is a universally assigned address.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is a universally assigned address;
 *   false (0) otherwise.
 */
static inline int rte_is_universal_ether_addr(const struct rte_ether_addr *ea)
{
	return (ea->addr_bytes[0] & RTE_ETHER_LOCAL_ADMIN_ADDR) == 0;
}

/**
 * Check if an Ethernet address is a locally assigned address.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is a locally assigned address;
 *   false (0) otherwise.
 */
static inline int rte_is_local_admin_ether_addr(const struct rte_ether_addr *ea)
{
	return (ea->addr_bytes[0] & RTE_ETHER_LOCAL_ADMIN_ADDR) != 0;
}

/**
 * Check if an Ethernet address is a valid address. Checks that the address is a
 * unicast address and is not filled with zeros.
 *
 * @param ea
 *   A pointer to a ether_addr structure containing the ethernet address
 *   to check.
 * @return
 *   True  (1) if the given ethernet address is valid;
 *   false (0) otherwise.
 */
static inline int rte_is_valid_assigned_ether_addr(const struct rte_ether_addr *ea)
{
	return rte_is_unicast_ether_addr(ea) && (!rte_is_zero_ether_addr(ea));
}

/**
 * Generate a random Ethernet address that is locally administered
 * and not multicast.
 * @param addr
 *   A pointer to Ethernet address.
 */
void
rte_eth_random_addr(uint8_t *addr);

/**
 * Fast copy an Ethernet address.
 *
 * @param ea_from
 *   A pointer to a ether_addr structure holding the Ethernet address to copy.
 * @param ea_to
 *   A pointer to a ether_addr structure where to copy the Ethernet address.
 */
static inline void rte_ether_addr_copy(const struct rte_ether_addr *ea_from,
				   struct rte_ether_addr *ea_to)
{
#ifdef __INTEL_COMPILER
	uint16_t *from_words = (uint16_t *)(ea_from->addr_bytes);
	uint16_t *to_words   = (uint16_t *)(ea_to->addr_bytes);

	to_words[0] = from_words[0];
	to_words[1] = from_words[1];
	to_words[2] = from_words[2];
#else
	/*
	 * Use the common way, because of a strange gcc warning.
	 */
	*ea_to = *ea_from;
#endif
}

/**
 * Ethernet header: Contains the destination address, source address
 * and frame type.
 */
struct rte_ether_hdr {
    struct rte_ether_addr d_addr; /**< Destination address. */
    struct rte_ether_addr s_addr; /**< Source address. */
    uint16_t ether_type;      /**< Frame type. */
} __attribute__ ((aligned(2)));

/* Ethernet frame types */
#define RTE_ETHER_TYPE_IPV4 0x0800 /**< IPv4 Protocol. */
#define RTE_ETHER_TYPE_IPV6 0x86DD /**< IPv6 Protocol. */
#define RTE_ETHER_TYPE_ARP  0x0806 /**< Arp Protocol. */
#define RTE_ETHER_TYPE_RARP 0x8035 /**< Reverse Arp Protocol. */
#define RTE_ETHER_TYPE_VLAN 0x8100 /**< IEEE 802.1Q VLAN tagging. */

#ifdef __cplusplus
}
#endif

#endif /* _RTE_ETHER_H_ */
