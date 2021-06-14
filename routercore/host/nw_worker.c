#include "nw_worker.h"

#include <bstgw/pta_trustrouter.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

static inline int get_nworkers(struct bstgw_router *);

static void *thread_run_worker(void *);
static inline void run_worker(struct bstgw_rtr_thread *);

/* returns number of workers expected/supported by the secure router */
static inline int get_nworkers(struct bstgw_router *rtr) {
    return get_thread_number(rtr, BSTGW_PTA_TRUST_ROUTER_CMD_GET_NWORKERS);
}

static void *thread_run_worker(void *arg) {
    if (arg == NULL) return NULL;
    run_worker((struct bstgw_rtr_thread *)arg);
    return NULL;
}

/* start the router/firewall worker thread(s) */
// TODO: pass available cpuset
int start_workers(struct bstgw_router *rtr, cpu_set_t *mask_ptr) {
    printf("Starting Firewall worker threads\n");
    if (rtr == NULL) return -1;

    int nworkers = get_nworkers(rtr);
    if (nworkers <= 0) return -1;

    cpu_set_t wrk_mask;
    bool split_mask = CPU_COUNT(mask_ptr) >= nworkers;

    if (!split_mask) {
        fprintf(stderr, "Warning: less worker cores than workers "
            "(nwrks: %d vs. avail: %d)\n", nworkers, CPU_COUNT(mask_ptr));
    }

    for(int i=0; i<nworkers; i++) {
        struct bstgw_rtr_thread *rthrd = spawn_router_thread(thread_run_worker, BSTGW_TRUST_ROUTER_WORKER_SESSION);
        if (rthrd == NULL) {
            // TODO: cleanup in failure
            fprintf(stderr, "Failed spawning router thread\n"); abort();
        }

        CPU_ZERO(&wrk_mask);
        // share full mask
        if (!split_mask) {
            CPU_XOR(&wrk_mask, &wrk_mask, mask_ptr);
        } else {
            // each worker gets 1 (separate) CPU of the given mask
            int req_cpu_hits=(i+1);
            for (int c=0; c<sysconf(_SC_NPROCESSORS_ONLN); c++) {
                if (CPU_ISSET(c, mask_ptr)) {
                    req_cpu_hits--;
                    if (req_cpu_hits == 0) {
                        CPU_SET(c, &wrk_mask);
                        printf("Assigning CPU %d to worker %d\n", c, i);
                        break;
                    }
                }
            }
            // Error: not successful (did #processors change?)
            if (req_cpu_hits > 0) {
                fprintf(stderr, "ERROR: Failed to get unique CPU for worker #%d; "
                    "Using full mask for it instead\n", i);
                CPU_XOR(&wrk_mask, &wrk_mask, mask_ptr);
            }
        }

        // CPU affinity of worker
        if (pthread_setaffinity_np(rthrd->thread_id, sizeof(cpu_set_t), &wrk_mask) != 0) {
            fprintf(stderr, "Warning: failed to set affinity of worker thread #%d\n", i);
        }
        SLIST_INSERT_HEAD(&rtr->worker_list, rthrd, entry);     
    }
    return 0;
}

static inline void run_worker(struct bstgw_rtr_thread *rthrd) {
    if (rthrd == NULL) return;

    printf("Going to run firewall thread\n");

    uint32_t err_origin;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);

    /* RUN */
    while (!stop) {
        if (TEEC_SUCCESS != TEEC_InvokeCommand(&rthrd->sess, BSTGW_PTA_TRUST_ROUTER_CMD_RUN_WORKER,
            &op, &err_origin)) {
                printf("Failed to run SW Worker Thread (errOrigin: %u)\n", err_origin);
                break;
        }
    }
}
