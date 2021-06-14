#ifndef BSTGW_CLIENT_HPP
#define BSTGW_CLIENT_HPP

#include <tee_client_api.h>

#include "shm_rb.h"

#include <pthread.h>

struct bstgw_http_conn {
    int cli_fd;
    struct bstgw_http_conn *next;
    pthread_t phid;
    
    TEEC_Context ctx; // TODO: unclear whether ctx would be thread-safe
    TEEC_Session sess;

    shm_rb_t rx_buffer;
    shm_rb_t tx_buffer;
};

// TODO: how to connect to server config in OP-TEE impl. ?!
struct bstgw_http_conn *nw_init_tls_client();
int nw_fini_tls_client(struct bstgw_http_conn *);

//    const mbedtls_ssl_config *config; || passed by server main thread

void *nw_thread_client_conn(void *);
int nw_handle_client_conn(struct bstgw_http_conn *);

#endif /* BSTGW_CLIENT_HPP */
