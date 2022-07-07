#include "nw_router_thread.h"
#include "bstgw/pta_trustrouter.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct bstgw_rtr_thread *nw_init_router_thread(int type) {
    struct bstgw_rtr_thread *rthrd = malloc(sizeof(struct bstgw_rtr_thread));
    if (rthrd == NULL) return rthrd;

    if (TEEC_SUCCESS != TEEC_InitializeContext(NULL, &rthrd->ctx)) goto InitErr;

    uint32_t err_origin;
    TEEC_UUID uuid = BSTGW_PTA_TRUST_ROUTER_UUID;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));

    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = type;

    if (TEEC_SUCCESS != TEEC_OpenSession(&rthrd->ctx, &rthrd->sess, &uuid,
        TEEC_LOGIN_PUBLIC, NULL, &op, &err_origin)) {
            printf("Session init error from origin: %u\n", err_origin);
            goto SessionErr;
    }
    return rthrd;

SessionErr:
    TEEC_FinalizeContext(&rthrd->ctx);
InitErr:
    free(rthrd);
    return NULL;
}

int nw_fini_router_thread(struct bstgw_rtr_thread *rthrd) {
    if (rthrd == NULL) return -1;
    TEEC_CloseSession(&rthrd->sess);
    TEEC_FinalizeContext(&rthrd->ctx);
    free(rthrd);
    return 0;
}