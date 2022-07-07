/*
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 * $FreeBSD: src/sys/netinet/in.h,v 1.68 2002/04/24 01:26:11 mike Exp $
 */

#ifndef _NEWLIB_NETINET_IN_H_
#define _NEWLIB_NETINET_IN_H_

#include <sys/cdefs.h>
//#include <sys/config.h>
//#include <sys/_types.h>
//#include <machine/endian.h>
#include <newlib/endian.h>

/* Protocols common to RFC 1700, POSIX, and X/Open. */
#define	IPPROTO_IP		0		/* dummy for IP */
#define	IPPROTO_ICMP		1		/* control message protocol */
#define	IPPROTO_TCP		6		/* tcp */
#define	IPPROTO_UDP		17		/* user datagram protocol */


/* WARNING: BSD stuff */
#define	IPPROTO_HOPOPTS		0		/* IP6 hop-by-hop options */
#define	IPPROTO_ROUTING		43		/* IP6 routing header */
#define	IPPROTO_FRAGMENT	44		/* IP6 fragmentation header */
#define	IPPROTO_GRE			47		/* General Routing Encap. */
#define	IPPROTO_ICMPV6		58		/* ICMP6 */
#define	IPPROTO_DSTOPTS		60		/* IP6 destination option */
#define IPPROTO_MAX			256
//


#define	INADDR_ANY		(u_int32_t)0x00000000
#define	INADDR_BROADCAST	(u_int32_t)0xffffffff	/* must be masked */

#ifndef _IN_ADDR_T_DECLARED
typedef	uint32_t		in_addr_t;
#define	_IN_ADDR_T_DECLARED
#endif

#ifndef _IN_PORT_T_DECLARED
typedef	uint16_t		in_port_t;
#define	_IN_PORT_T_DECLARED
#endif

typedef unsigned short sa_family_t;

/* Internet address (a structure for historical reasons). */
#ifndef	_STRUCT_IN_ADDR_DECLARED
struct in_addr {
	in_addr_t s_addr;
};
#define	_STRUCT_IN_ADDR_DECLARED
#endif

/* Socket address, internet style. */
struct sockaddr_in {
	sa_family_t	sin_family;
	in_port_t	sin_port;
	struct	in_addr sin_addr;
	char	sin_zero[8];
};

//#if __POSIX_VISIBLE >= 200112
#define	IPPROTO_RAW		255		/* raw IP packet */
#define	INET_ADDRSTRLEN		16

/* INET6 stuff */
#define	__NEWLIB_KAME_NETINET_IN_H_INCLUDED_
#include <newlib/netinet6/in6.h>
#undef __NEWLIB_KAME_NETINET_IN_H_INCLUDED_

#endif /* !_NEWLIB_NETINET_IN_H_*/
