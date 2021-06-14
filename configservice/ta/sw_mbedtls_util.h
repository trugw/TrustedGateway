#ifndef BSTGW_SW_MBEDLTS_H
#define BSTGW_SW_MBEDLTS_H

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/certs.h"
#include "mbedtls/x509.h"
#include "mbedtls/ssl.h"
#include "mbedtls/error.h"

#include "bstgw/ringbuffer.h"

/* template for OP-TEE TA Sessions */
struct sw_server_session {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt srvcert;
    mbedtls_x509_crt cachain;
    mbedtls_pk_context pkey;
};
typedef struct sw_server_session sw_srv_t;

/* setup/teardown mbedTLS server structs */
int sw_init_mbedtls_srv(sw_srv_t *);
int sw_fini_mbedtls_srv(sw_srv_t *);

typedef struct flat_buffer {
    // warning: current HTTP parser is stateless and we don't chain
    // flat_buffers, so the buffer size currently dictates the maxium
    // request length
    char buffer[4096];
    uint32_t next; /* TODO: changed from int -> uint32_t; CHECK USAGE! */
} sw_fltbuf_t;

int init_flt_buffer(sw_fltbuf_t *);
int free_flt_buffer(sw_fltbuf_t *);
int spaceleft_flt_buffer(const sw_fltbuf_t *);

struct resp_chunk; // response_queue.h

// TODO: unclear how server-client session are connected (or maybe will be the same?!)
struct sw_http_client {
    sw_fltbuf_t http_req_buffer; // TODO: consider replacing with "flat" array buffer
    int is_partial_request;
    struct resp_chunk *resp_buffer;
};

struct sw_client_session {
    const mbedtls_ssl_config *config; // TODO: how to connect in OP-TEE TA to Server config ?!
    mbedtls_ssl_context ssl;
    //sw_buff_t rx_buffer; // TODO: belongs into normal world / shared memory
    //sw_buff_t tx_buffer; // TODO: belongs into normal world / shared memory

    sw_buff_t *rx_buf_ptr;
    sw_buff_t *tx_buf_ptr;

    struct sw_http_client sw_http_cli; // this is SW-exclusive
};
typedef struct sw_client_session sw_cli_t;

/* setup/teardown mbedTLS client SSL context */
int sw_init_mbedtls_cli(sw_cli_t *);
int sw_fini_mbedtls_cli(sw_cli_t *);


/* custom mbedTLS BIO API for send/recv */

// Write at most 'len' characters
int bstgw_mbedtls_net_send( void *ctx, const unsigned char *buf, size_t len );
// Read at most 'len' characters
int bstgw_mbedtls_net_recv( void *ctx, unsigned char *buf, size_t len );

#endif /* BSTGW_SW_MBEDLTS_H */
