#include <stdlib.h>
#include <stdio.h>

#include "server.h"

int main(int argc, char *argv[]) {
    if (argc > 1) return EXIT_FAILURE;
    printf("Welcome\n");

    struct bstgw_http_server *srv = NULL;

    if ((srv = nw_init_https_server()) == NULL) {
        printf("Failed initialization\n");
        return EXIT_FAILURE;
    }

    if (nw_start_http_listening(srv)) {
        printf("Failed listening\n");
        return EXIT_FAILURE;
    }
    printf("Started listening on tcp/4433\n");

    if (nw_loop_http_listening(srv)) {
        printf("Failed looping");
        nw_stop_http_listening(srv);
        return EXIT_FAILURE;
    }

    nw_stop_http_listening(srv);
    printf("Stopped listening on tcp/4433\n");

    nw_fini_https_server(srv);

    return EXIT_SUCCESS;
}