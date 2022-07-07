#define _GNU_SOURCE
#include <sched.h>

#include "router.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include <unistd.h>

#include <bstgw/pta_trustrouter.h>

#include "nw_cleaner.h"
#include "nw_worker.h"

volatile sig_atomic_t	stop = false;

/* init nw and sw router parts */
struct bstgw_router *nw_init_router(void) {
    struct bstgw_router *rtr = malloc(sizeof(struct bstgw_router));
    if (rtr == NULL) return rtr;

    /* Init TEE and TA Connections required to init the TA-side router */
    if (TEEC_SUCCESS != TEEC_InitializeContext(NULL, &rtr->ctx)) goto RtrInitErr;

    uint32_t err_origin;
    TEEC_UUID uuid = BSTGW_PTA_TRUST_ROUTER_UUID;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));

    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = BSTGW_TRUST_ROUTER_UNBOUND_SESSION;

    if (TEEC_SUCCESS != TEEC_OpenSession(&rtr->ctx, &rtr->sess, &uuid,
        TEEC_LOGIN_PUBLIC, NULL, &op, &err_origin)) {
            printf("Session init error from origin: %u\n", err_origin);
            goto SessionErr;
    }

    rtr->npf_cleaner = NULL;
    SLIST_INIT(&rtr->worker_list);

    return rtr;

SessionErr:
    TEEC_FinalizeContext(&rtr->ctx);
RtrInitErr:
    free(rtr);
    return NULL;
}

/* free nw and sw router parts */
int nw_fini_router(struct bstgw_router *rtr) {
    if (rtr == NULL) return -1;
    TEEC_CloseSession(&rtr->sess);
    TEEC_FinalizeContext(&rtr->ctx);
    free(rtr);
    return 0;
}

// todo: consider hyper-threading
static int prepare_cpu_masks(cpu_set_t *clnr_msk_ptr, cpu_set_t *wrkr_msk_ptr) {
    CPU_ZERO(clnr_msk_ptr);
    CPU_ZERO(wrkr_msk_ptr);

    cpu_set_t parent_mask;
    CPU_ZERO(&parent_mask);
    int ret = sched_getaffinity(0, sizeof(parent_mask), &parent_mask);
    if(ret != 0) {
        perror("sched_getaffinity failed");
        return -1;
    }

    // single core-only
    if (sysconf(_SC_NPROCESSORS_ONLN) == 1) {
        CPU_XOR(clnr_msk_ptr, clnr_msk_ptr, &parent_mask);
        CPU_XOR(wrkr_msk_ptr, wrkr_msk_ptr, &parent_mask);
        return 0;
    }

    // cleaner: CPU #0, workers: rest
    CPU_SET(0, clnr_msk_ptr);
    CPU_XOR(wrkr_msk_ptr, wrkr_msk_ptr, &parent_mask);
    CPU_CLR(0, wrkr_msk_ptr);
    return 0;
}

/* will spawn all threads, set their per-core affinities, start them and wait for
 * termination or an external (user) signal */
int nw_runloop_router(struct bstgw_router *rtr) {
    if (rtr == NULL) return -1;

    // prepare core pinning (cleaner -> 0, workers -> rest)
    cpu_set_t cleaner_mask, worker_mask;
    if (prepare_cpu_masks(&cleaner_mask, &worker_mask) != 0) {
        fprintf(stderr, "Failed CPU pinning preparation\n");
        abort();
    }

    /* TODO: Cleanup !! */
    if (start_npf_cleaner(rtr, &cleaner_mask) != 0) { fprintf(stderr, "Failed starting cleaner threads\n"); abort(); }
    if (start_workers(rtr, &worker_mask) != 0) { fprintf(stderr, "Failed starting worker threads\n"); abort(); }
    
    // Wait for thread termination (TODO: or signal!?)
    printf("Waiting for termination of firewall threads\n");
    struct bstgw_rtr_thread *rthrd;
    SLIST_FOREACH(rthrd, &rtr->worker_list, entry) {
        pthread_join(rthrd->thread_id, NULL);
        nw_fini_router_thread(rthrd);
	}

    // TODO: how to signal termination?
    printf("Waiting for termination of NPF worker thread\n");
    pthread_join(rtr->npf_cleaner->thread_id, NULL);
    nw_fini_router_thread(rtr->npf_cleaner);
    rtr->npf_cleaner = NULL;
    return 0;
}

int get_thread_number(struct bstgw_router *rtr, int cmd) {
    if (rtr == NULL) return -1;

    uint32_t err_origin;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));

    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&rtr->sess, cmd,
        &op, &err_origin)) return -1;

    return op.params[0].value.a;
}

// TODO: pass available cpuset for setting the affinities
struct bstgw_rtr_thread *spawn_router_thread(void *(*thread_entry)(void *), int type) {
    if (thread_entry == NULL) return NULL;
    struct bstgw_rtr_thread *rthrd = nw_init_router_thread(type);
    if (rthrd != NULL) {
        if (0 != pthread_create(&rthrd->thread_id, NULL, thread_entry, rthrd)) {
            nw_fini_router_thread(rthrd);
            rthrd = NULL;
        }
    }
    return rthrd;
}
