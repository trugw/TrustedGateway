#include "nw_cleaner.h"

#include <bstgw/pta_trustrouter.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void *thread_run_cleaner(void *);
static inline void run_cleaner(struct bstgw_rtr_thread *);

static void *thread_run_cleaner(void *arg) {
    if (arg == NULL) return NULL;
    run_cleaner((struct bstgw_rtr_thread *)arg);
    return NULL;
}

/* start the NPF kern cleanup thread(s) */
// TODO: pass available cpuset
int start_npf_cleaner(struct bstgw_router *rtr, cpu_set_t *mask_ptr) {
    printf("Starting NPF Cleaner thread\n");
    if (rtr == NULL || rtr->npf_cleaner != NULL) return -1;

    rtr->npf_cleaner = spawn_router_thread(thread_run_cleaner, BSTGW_TRUST_ROUTER_CLEANER_SESSION);
    if (rtr->npf_cleaner == NULL) {
        fprintf(stderr, "Failed spawning cleaner thread");
        return -1;
    }

    // CPU affinity of thread
    if (pthread_setaffinity_np(rtr->npf_cleaner->thread_id,
        sizeof(cpu_set_t), mask_ptr) != 0) {
        fprintf(stderr, "Warning: failed to set affinity of cleaner thread\n");
    }

    return 0;
}

static inline void run_cleaner(struct bstgw_rtr_thread *rthrd) {
    if (rthrd == NULL) return;

    printf("Going to run cleaner thread\n");

    uint32_t err_origin;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

    /* RUN */
    while (!stop) {
        if (TEEC_SUCCESS != TEEC_InvokeCommand(&rthrd->sess, BSTGW_PTA_TRUST_ROUTER_CMD_RUN_CLEANER,
            &op, &err_origin)) {
                printf("Failed to run SW Cleaner Thread (errOrigin: %u)\n", err_origin);
                break;
        }
    }
}
