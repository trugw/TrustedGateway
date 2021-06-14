#ifndef BSTGW_NW_CLEANER_H
#define BSTGW_NW_CLEANER_H

#include "router.h"

#define _GNU_SOURCE
#include <sched.h>

int start_npf_cleaner(struct bstgw_router *, cpu_set_t *);

#endif /* BSTGW_NW_CLEANER_H */
