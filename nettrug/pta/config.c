/*
 * Copyright 2022 Fabian Schwarz and CISPA Helmholtz Center for Information Security
 * All rights reserved.
 *
 * Use is subject to the license terms, as specified in the project LICENSE file.
 *
 * ----------------------------------------------------------------------
 *
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the NPF_LICENSE file.
 */

#include <string.h>
#include <trace.h>

#include <lpm.h>

#include "npf_router.h"
#include "utils.h"

#include <newlib/strings.h>
#include <newlib/arpa/inet.h>

#include <newlib/linux/sys/socket.h> // AF_INET

/* Config syntax:
 *
 * 	-- add <ip> to list of IP address of <if-name> --
 * 
 * 	ifconfig <if-name> <ip>
 * 
 * 
 *  -- packets destined to <dst-ip/cidr> leave through <out-if-name>
 *     the next-hop is either the optional <next-hop-ip>, or the dst IP
 * 	   for ARP request (for resolving next dst MAC), use <out-if-name> IP
 * 	   with index <ifnet-ip-idx> as source IP (note: idx 0 == default IP) --
 * 
 * 	route <dst-ip/cidr> <out-if-name> [<ifnet-ip-idx> [<next-hop-ip>]]
 * 
 * 
 * FIXME:  add netmask support; then kick <ifnet-ip-idx>
 */

/*
static const char *router_config_str = "ifconfig NW 172.16.0.1\n"
	"route 0.0.0.0/0 NW 172.16.0.44\n";
*/

/*
 * Linux == interface as exposed to Linux as virtual NIC
 * Ext.  == interface to external world, based on real NIC
 * NW    == virtual, internal interface as bridge to/from Linux
 * 
 * Linux:  IP 192.168.178.49, MAC: 00:19:B8:08:40:78 <- set by VNIC
 * Ext.:   IP 192.168.178.49, MAC: 00:19:B8:08:40:78 <- real HW MAC
 * NW:     IP 192.168.178.11, MAC: fa:fa:de:ad:be:ef <- virt; set by VNIC
 * FritzBox router (gateway):  192.168.178.1
 *
 * Non-FRITZ!Box:
 * Linux:  IP 172.16.0.44, MAC: 00:19:B8:08:40:78 <- set by VNIC
 * Ext.:   IP 172.16.0.44, MAC: 00:19:B8:08:40:78 <- real HW MAC
 * NW:     IP 172.16.0.1,  MAC: fa:fa:de:ad:be:ef <- virt; set by VNIC
 * 
 * Rules:
 * - packets targeting the router IP go to the NW
 * - everything else goes to the outer world
 */
/* // with fritz!box
static const char *router_config_str = "ifconfig NW 192.168.178.11\n"
	"ifconfig imx6q-fec 192.168.178.49\n"
	"route 192.168.178.49/32 NW\n"
	"route 0.0.0.0/0 imx6q-fec 0 192.168.178.1\n"; // <real-gw-ip>\n";
*/

/* // single IP
static const char *router_config_str = "ifconfig NW 192.168.178.11\n"
	"ifconfig imx6q-fec 192.168.178.49\n"
	"route 192.168.178.49/32 NW\n"
	"route 192.168.178.0/24 imx6q-fec\n";
*/

/*
 * Non-FRITZ!Box:
 * Linux:  IP 192.168.178.49, MAC: 00:19:B8:08:40:78 <- set by VNIC
 * + Ext   IP 172. 16.  0.49
 * 
 * NW:     IP 192.168.178.11,  MAC: fa:fa:de:ad:be:ef <- virt; set by VNIC
 * 		   IP 172. 16.  0.11
 * 
 * MAC:    IP 192.168.178. 4/24
 * 
 * HP:     IP 172. 16.  0. 7/24
 */
static const char *router_config_str = "ifconfig NW 192.168.178.11\n"
	"ifconfig NW 172.16.0.11\n"
	"ifconfig imx6q-fec 192.168.178.49\n"
	"ifconfig imx6q-fec 172.16.0.49\n"
	"route 192.168.178.49/32 NW 0\n"
	"route 172.16.0.49/32 NW 1\n"
	"route 192.168.178.0/24 imx6q-fec 0\n"
	"route 172.16.0.0/24 imx6q-fec 1\n";
//	"route 0.0.0.0/0 imx6q-fec 0 192.168.178.1\n";

/*
// 2 subnetworks, but with fritz!box
static const char *router_config_str = "ifconfig NW 192.168.178.11\n"
	"ifconfig NW 172.16.0.11\n"
	"ifconfig imx6q-fec 192.168.178.49\n"
	"ifconfig imx6q-fec 172.16.0.49\n"
	"route 192.168.178.49/32 NW 0\n"
	"route 172.16.0.49/32 NW 1\n"
	"route 192.168.178.0/24 imx6q-fec 0\n"
	"route 172.16.0.0/24 imx6q-fec 1\n"
	"route 0.0.0.0/0 imx6q-fec 0 192.168.178.1\n";
*/

static unsigned
str_tokenize(char *line, char **tokens, unsigned n)
{
	const char *sep = " \t";
	unsigned i = 0;
	char *token;

	while ((token = strsep(&line, sep)) != NULL && i < n) {
		if (*sep == '\0' || strpbrk(token, sep) != NULL) {
			continue;
		}
		tokens[i++] = token;
	}
	return i;
}

static int
parse_command(npf_router_t *router, char **tokens, size_t n)
{
	int if_idx;

	if (strcasecmp(tokens[0], "route") == 0) {
		route_table_t *rtable = router->rtable;
		unsigned char addr[16];
		route_info_t rt;
		unsigned plen;
		size_t alen;

		if (n < 3) {
			DMSG("route too few arguments");
			return -1;
		}
		if (lpm_strtobin(tokens[1], addr, &alen, &plen) == -1) {
			DMSG("CIDR to bin failed");
			return -1;
		}
		if ((if_idx = ifnet_nametoindex(tokens[2])) <= 0) {
			EMSG("unknown interface '%s'", tokens[2]);
			return -1;
		}

		memset(&rt, 0, sizeof(route_info_t));
		rt.if_idx = if_idx;
		if (n >= 4) {
			// src IP for ARP requests
			ifnet_t *ifp = ifnet_getbyidx(if_idx);
			if(!ifp) panic("assertion(ifp != NULL)");

			unsigned int idx = (unsigned int) strtoul(tokens[3], NULL, 10);
			if (idx >= ifp->ips_cnt) {
				EMSG("ifnet index not in used range (idx: %u, #ips: %u",
					idx, ifp->ips_cnt);
				return -1;
			}
			rt.if_addr_idx = idx;

			if (n >= 5) {
				// only ipv4 atm
				if (inet_pton(AF_INET, tokens[4], &rt.next_hop) == -1) {
					EMSG("invalid gateway '%s'", tokens[4]);
					return -1;
				}
				rt.addr_len = sizeof(in_addr_t); //xx
			}
		}
		if (route_add(rtable, addr, alen, plen, &rt) == -1) {
			DMSG("route_add failed");
			return -1;
		}

		DMSG("Added new routing rule: route %s %s --> %s [ip-idx: %s]",
			tokens[1], tokens[2], (n>=5) ? tokens[4] : "dstIP",
			(n>=4) ? tokens[3] : "0");
		return 0;
	}
	if (strcasecmp(tokens[0], "ifconfig") == 0) {
		if (n < 2) {
			return -1;
		}
		if ((if_idx = ifnet_nametoindex(tokens[1])) <= 0) {
			EMSG("unknown interface '%s'", tokens[1]);
			return -1;
		}
		ifnet_t *ifp = ifnet_getbyidx(if_idx);
		if(!ifp) panic("assertion(ifp != NULL)");

		// first IP becomes the default address
		if (ifp->ips_cnt >= BSTGW_MAX_NETIF_IPS) {
			EMSG("too many IPs for interface: %s", ifp->name);
			return -1;
		}

		npf_addr_t *ipaddr_ptr = &ifp->if_ips[ifp->ips_cnt];

		// only ipv4 atm
		if (inet_pton(AF_INET, tokens[2], ipaddr_ptr) == -1) {
			EMSG("invalid IPv4 address '%s'", tokens[2]);
			return -1;
		}
		ifp->ips_cnt++;

		IMSG("Configured %s interface with new IP %s (0x%x) [idx: %u]",
			ifp->name, tokens[2], ipaddr_ptr->word32[0], ifp->ips_cnt - 1);
		return 0;
	}
	return -1;
}

static int
parse_config(npf_router_t *router, const char *config_str)
{
	char *cfg_dup = strdup(config_str);
	if (cfg_dup == NULL) {
		EMSG("Config string duplication failed");
		return -1;
	}

	size_t ln = 0, n;
	const char *sep = "\n";
	char *line;
	char *rest_str = cfg_dup;

	while (rest_str != NULL && (line = strsep(&rest_str, sep)) != NULL ) {
		char *tokens[] = { NULL, NULL, NULL, NULL, NULL };
		ln++;

		if (line[0] == '#') {
			continue;
		}

		n = str_tokenize(line, tokens, __arraycount(tokens));
		if (n == 0) {
			continue;
		}
		if (parse_command(router, tokens, n) == -1) {
			EMSG("invalid command at line %zu", ln);
			free(cfg_dup);
			return -1;
		}

	}

	free(cfg_dup);
	return 0;
}

int
load_config(npf_router_t *router)
{
	const char *config = router_config_str;
	assert(router && config);
	return parse_config(router, config);
}
