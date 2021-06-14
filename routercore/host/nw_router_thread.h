#ifndef BSTGW_ROUTER_THREAD_H
#define BSTGW_ROUTER_THREAD_H

#include <tee_client_api.h>

#define _GNU_SOURCE
#include <pthread.h>

#include <sys/queue.h>

struct bstgw_rtr_thread {
    pthread_t thread_id;
    TEEC_Context ctx; // TODO: can it be shared?!
    TEEC_Session sess;

    SLIST_ENTRY(bstgw_rtr_thread) entry;
};

struct bstgw_rtr_thread *nw_init_router_thread(int type);
int nw_fini_router_thread(struct bstgw_rtr_thread *rthrd);

#endif /* BSTGW_ROUTER_THREAD_H */