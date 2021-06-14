#ifndef BSTGW_SECURE_TA_H
#define BSTGW_SECURE_TA_H

struct sw_server_session;
struct mbedtls_ssl_config;

/* setup server */
int sw_init_tls_srv(struct sw_server_session **);
const struct mbedtls_ssl_config *get_tls_srv_conf(struct sw_server_session *);
int sw_fini_tls_srv(struct sw_server_session *);

struct sw_client_session;

/* setup client */
int sw_init_tls_cli(struct sw_client_session **, const struct mbedtls_ssl_config *);
int sw_fini_tls_cli(struct sw_client_session *);

/* new (temporary) helpers */
int set_rxtx_buffers(struct sw_client_session *, void *, void *);
int clear_rxtx_buffers(struct sw_client_session *);

/* handshake and processing */
int sw_do_handshake_cli(struct sw_client_session *);
int sw_read_https_req_data(struct sw_client_session *);

// unused
int sw_notify_transport_close(struct sw_client_session *);
int sw_notify_transport_error(struct sw_client_session *, int err);

/* http handling */
int sw_init_http_cli(struct sw_client_session *);
int sw_fini_http_cli(struct sw_client_session *);

#endif /* BSTGW_SECURE_TA_H */