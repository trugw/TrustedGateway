#include <stdlib.h>
#include <stdio.h>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <string.h>

#include <signal.h>

#include "server.h"
#include "client.h"

#include "webapp_loading.h"

#include <bstgw/web_stub_ta.h>

volatile sig_atomic_t stop = 0;
void signal_handler(int signal) {
    if (signal == SIGINT) stop = 1;
}

/* init nw and sw HTTPS server parts */
struct bstgw_http_server *nw_init_https_server() {
    struct bstgw_http_server *srv = malloc(sizeof(struct bstgw_http_server));
    if (srv == NULL) return srv;
    srv->fd = -1;
    srv->head = NULL;

    /* Init TEE and TA Connections required to init the TA-side TLS server */
    if (TEEC_SUCCESS != TEEC_InitializeContext(NULL, &srv->ctx)) goto SrvInitErr;

    uint32_t err_origin;
    TEEC_UUID uuid = BSTGW_TA_WEB_STUB_UUID;
    TEEC_Operation op;

    // server session type
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = BSTGW_WEBSTUB_SERVER_SESSION;
    
    if (TEEC_SUCCESS != TEEC_OpenSession(&srv->ctx, &srv->sess, &uuid,
        TEEC_LOGIN_PUBLIC, NULL, &op, &err_origin)) {
            printf("Session init error from origin: %u\n", err_origin);
            goto SessionErr;
    }

    /* Now init the TA-side TLS server */
    memset(&op, 0, sizeof(op)); // cleanup before reuse
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&srv->sess, BSTGW_TA_WEB_STUB_CMD_INIT_HTTPS_SERVER,
        &op, &err_origin)) {
            printf("Failed to init SW-mbedTLS (errOrigin: %u)\n", err_origin);
            goto MbedInitErr;
    }

    /* New (temp?):  bootstrap HTML files */
    // TODO: make this optional depending on whether it already exists in secure storage
    // -- could add return value to above "NEED_HTML_BOOTSTRAP" with type list or sth. like that
    if (0 != bootstrap_webapps(&srv->sess)) {
        printf("Failed to bootstrap webapps\n");
        nw_fini_https_server(srv);
        return NULL;
    }

    return srv;

MbedInitErr:
    TEEC_CloseSession(&srv->sess);
SessionErr:
    TEEC_FinalizeContext(&srv->ctx);
SrvInitErr:
    free(srv);
    return NULL;
}

/* free nw and sw HTTPS server parts */
int nw_fini_https_server(struct bstgw_http_server *srv) {
    if (srv == NULL) return -1;
    
    /* Request fini of SW TLS Server */
    uint32_t err_origin;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&srv->sess, BSTGW_TA_WEB_STUB_CMD_FINI_HTTPS_SERVER,
        &op, &err_origin)) {
        printf("WARNING: Failed to fini SW TLS Server (errOrigin: %u)\n", err_origin);
    }   
    
    TEEC_CloseSession(&srv->sess);
    TEEC_FinalizeContext(&srv->ctx);
    free(srv);
    return 0;
}

/* setup TCP server socket */
int nw_start_http_listening(struct bstgw_http_server *srv) {
    if (srv == NULL) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) goto error;

    struct sockaddr_in httpd_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(4433),
    };

    /*
    const char *bind_addr = "127.0.0.1";
    if (inet_aton(bind_addr, &httpd_addr.sin_addr) == 0) {
        goto sock_err;
    }
    */
   httpd_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&httpd_addr, sizeof(httpd_addr)) == -1) {
        perror("bind failed");
        goto sock_err;
    }

    if (listen(fd, 1) == -1) {
        goto sock_err;
    }

    srv->fd = fd;
    return 0;
sock_err:
    close(fd);
error:
    return -1;
}

/* listen for new client connections in a loop */
int nw_loop_http_listening(struct bstgw_http_server *srv) {
    if(srv == NULL) return -1;

    // signal handler
    struct sigaction sigact;
    sigact.sa_handler = signal_handler;
    sigemptyset (&sigact.sa_mask);
    sigact.sa_flags = 0;
    if( sigaction(SIGINT, &sigact, NULL) == 1 ) return -1;

    while (!stop) {
        printf("*****************************\nWaiting for new Client\n");

        struct sockaddr_in cli_addr;
        socklen_t cli_alen = sizeof(cli_addr);
        int cli_fd = 0;

        if((cli_fd = accept(srv->fd, (struct sockaddr *)&cli_addr, &cli_alen)) == -1)
            continue;
        printf("New client: %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        if(cli_addr.sin_family != AF_INET) {
            printf("Client not connected via IPv4\n");
        }

        // TODO: how to connect to server config in OP-TEE impl. ?!
        struct bstgw_http_conn *cli = nw_init_tls_client();
        if (cli == NULL) { close(cli_fd); continue; }

        cli->cli_fd = cli_fd;
        
        if(pthread_create(&cli->phid, NULL, nw_thread_client_conn, cli) != 0) {
            printf("Failed spawning thread for client connection\n");
            nw_fini_tls_client(cli);
            close(cli->cli_fd);
            continue;
        }

/*
        // append to connection list (TODO: we don't have to reimplement that)
        // TODO: move before pthread_create error and add removal to error case
        struct bstgw_http_conn *c = srv->head;
        if(c == NULL) {
            srv->head = cli;
        } else {
            for (; c->next != NULL; c = c->next);
            c->next = cli;
        }
*/

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);

        pthread_sigmask(SIG_BLOCK, &mask, NULL);

        printf("Waiting for client connection thread to finish\n");
        pthread_join(cli->phid, NULL);

        pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

        nw_fini_tls_client(cli);

/*
        // TODO
        c = srv->head;
        for (; c != NULL;) {
            struct bstgw_http_conn *n = c->next;
            nw_fini_tls_client(c);
            c = n;
        }
        srv->head = NULL;
*/

    }

    return 0;
}

/* close server socket */
int nw_stop_http_listening(struct bstgw_http_server *srv) {
    if (srv == NULL) return -1;
    return close(srv->fd);
}
