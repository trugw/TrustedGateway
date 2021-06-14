#ifndef BSTGW_SERVER_HPP
#define BSTGW_SERVER_HPP

#include <tee_client_api.h>

struct bstgw_http_server {
    int fd;
    struct bstgw_http_conn *head;
    TEEC_Context ctx;
    TEEC_Session sess;
};

struct bstgw_http_server *nw_init_https_server(void);
int nw_fini_https_server(struct bstgw_http_server *);

int nw_start_http_listening(struct bstgw_http_server *);
int nw_loop_http_listening(struct bstgw_http_server *);
int nw_stop_http_listening(struct bstgw_http_server *);

#endif /* BSTGW_SERVER_HPP */
