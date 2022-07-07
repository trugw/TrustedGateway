#include "nw_router_thread.h"
#include "router.h"
#include "bstgw/pta_trustrouter.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int router_stats(void) {
    printf("Initing session\n");
    struct bstgw_router *rtr_ctx = nw_init_router();
    if(!rtr_ctx) {
        fprintf(stderr, "Session opening failure.");
        return -1;
    }

    int stats = get_thread_number(rtr_ctx, BSTGW_PTA_TRUST_ROUTER_GET_STATS);
    printf("Router Stats: %d\n", stats);

    printf("Finish session\n");
    nw_fini_router(rtr_ctx);
    return 0;
}