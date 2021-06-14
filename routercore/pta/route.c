/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * Copyright (c) 2010-2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This material is based upon work partially supported by The
 * NetBSD Foundation under a contract with Mindaugas Rasiukevicius.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <lpm.h>

#include "npf_router.h"
#include "utils.h"

struct route_table {
	lpm_t *		lpm;
	unsigned	nitems;
};

typedef struct rtentry {
	unsigned	flags;
	route_info_t	rt;
} rtentry_t;

route_table_t *
route_table_create(void)
{
	route_table_t *rtbl;

	rtbl = calloc(1, sizeof(route_table_t));
	if (rtbl == NULL) {
        EMSG("OOM");
		return NULL;
	}
	rtbl->lpm = lpm_create();
	return rtbl;
}

static void
route_table_dtor(void *arg __unused, const void *key __unused,
    size_t len __unused, void *val)
{
	free(val);
}

void
route_table_destroy(route_table_t *rtbl)
{
	lpm_clear(rtbl->lpm, route_table_dtor, NULL);
	lpm_destroy(rtbl->lpm);
	free(rtbl);
}

int
route_add(route_table_t *rtbl, const void *addr, unsigned alen,
    unsigned preflen, const route_info_t *rt)
{
	rtentry_t *rtentry;

	if ((rtentry = calloc(1, sizeof(rtentry_t))) == NULL) {
        EMSG("OOM");
		return -1;
	}
	memcpy(&rtentry->rt, rt, sizeof(route_info_t));

	if (lpm_insert(rtbl->lpm, addr, alen, preflen, rtentry) == -1) {
		free(rtentry);
		return -1;
	}
	return 0;
}

int
route_lookup(route_table_t *rtbl, const void *addr, unsigned alen,
    route_info_t *output_rt)
{
	rtentry_t *rtentry;

	rtentry = lpm_lookup(rtbl->lpm, addr, alen);
	if (rtentry == NULL) {
		return -1;
	}
	memcpy(output_rt, &rtentry->rt, sizeof(route_info_t));
	return 0;
}
