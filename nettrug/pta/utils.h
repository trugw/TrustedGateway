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

#ifndef _UTILS_H_
#define _UTILS_H_

#include <assert.h>

/*
 * Branch prediction macros.
 */

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect(!!(x), 1)
#define	__predict_false(x)	__builtin_expect(!!(x), 0)
#endif

/*
 * Various C helpers and attribute macros.
 */

#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

static void bstgw_spin_cycles(unsigned int *count) {
    volatile unsigned int n = *count;
    while(__predict_true( n-- )) __asm volatile("yield" ::: "memory");
}

#endif
