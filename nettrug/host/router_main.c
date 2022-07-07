#include <stdlib.h>
#include <stdio.h>

#include <string.h>

#include "router.h"

static void
sighandler(int sig) {
	(void)sig;
	stop = true;
}

static void
setup_signals(void) {
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        if (!strcmp(argv[1], "stats")) return router_stats();
        return EXIT_FAILURE;
    }

    printf("Welcome\n");

    setup_signals();
    struct bstgw_router *rtr = NULL;

    printf("Going to initialize router\n");
    if ((rtr = nw_init_router()) == NULL) {
        printf("Failed initialization\n");
        return EXIT_FAILURE;
    }

    printf("Going to initialize the NPF and Firewalls threads + start run loops\n");
    if (nw_runloop_router(rtr) != 0) {
        printf("Router loop terminated with error\n");
    }

    nw_fini_router(rtr);
    return EXIT_SUCCESS;
}