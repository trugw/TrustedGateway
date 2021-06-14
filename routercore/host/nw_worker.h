#ifndef BSTGW_NW_WORKER_H
#define BSTGW_NW_WORKER_H

#define _GNU_SOURCE
#include <sched.h>

#include "router.h"

int start_workers(struct bstgw_router *, cpu_set_t *);

#endif /* BSTGW_NW_WORKER_H */
