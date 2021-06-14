#ifndef BSTGW_ROUTER_H
#define BSTGW_ROUTER_H

#include <tee_client_api.h>
#include "nw_router_thread.h"

#include <sys/queue.h>

#include <signal.h>

extern volatile sig_atomic_t	stop;

struct bstgw_router {
    TEEC_Context ctx;
    TEEC_Session sess;

    struct bstgw_rtr_thread *npf_cleaner;
    SLIST_HEAD(, bstgw_rtr_thread) worker_list;
};

struct bstgw_router *nw_init_router(void);
int nw_fini_router(struct bstgw_router *);

/* will spawn all threads, set their per-core affinities, start them and wait for
 * termination or an external (user) signal */
int nw_runloop_router(struct bstgw_router *);

int get_thread_number(struct bstgw_router *, int);
struct bstgw_rtr_thread *spawn_router_thread(void *(*thread_entry)(void *), int type);

/* New utils */
int router_stats(void);

#endif /* BSTGW_ROUTER_H */
