#include "sw_mbedtls_util.h"
#include "bstgw/ringbuffer.h"

#include <string.h>
#include <stdlib.h> // abort

#include <mbedtls/net_sockets.h> // MBEDTLS_ERR_SSL_WANT_READ, ...

#include <assert.h>

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#include <mbedtls/debug.h>
static void my_debug( void *ctx, int level, const char *file, int line, const char *str) {
    ((void) level);
    (void)ctx;
    DSMG( "%s:%04d: %s", file, line, str );
}
#endif

/* **************** */
/* SERVER FUNCTIONS */
/* **************** */
int sw_init_mbedtls_srv(sw_srv_t *sw_srv) {
    int ret;
    const char pers[] = "sw_server";

    if (sw_srv == NULL) return -1;

    mbedtls_x509_crt_init( &sw_srv->srvcert );
    mbedtls_x509_crt_init( &sw_srv->cachain );

    mbedtls_ssl_config_init( &sw_srv->conf );
    mbedtls_ctr_drbg_init( &sw_srv->ctr_drbg );

    // We use only a single entropy source that is used in all the threads.
    mbedtls_entropy_init( &sw_srv->entropy );

    // 1. Load the certificates and private RSA key
    IMSG( "\n  . Loading the server cert. and key..." );

    // use embedded test certificates
    ret = mbedtls_x509_crt_parse( &sw_srv->srvcert, (const unsigned char *) mbedtls_test_srv_crt,
                          mbedtls_test_srv_crt_len );
    if( ret != 0 ) {
        EMSG( " failed\n  !  mbedtls_x509_crt_parse (srv_cert) returned -0x%x\n\n", -ret );
        goto exit;
    }

    ret = mbedtls_x509_crt_parse( &sw_srv->cachain, (const unsigned char *) mbedtls_test_cas_pem,
                          mbedtls_test_cas_pem_len );
    //ret = mbedtls_x509_crt_parse_file( &sw_srv->cachain, "../keys/client.crt" );
    if( ret != 0 ) {
        EMSG( " failed\n  !  mbedtls_x509_crt_parse (cas_pem) returned -0x%x\n\n", -ret );
        goto exit;
    }

    mbedtls_pk_init( &sw_srv->pkey );
    ret =  mbedtls_pk_parse_key( &sw_srv->pkey, (const unsigned char *) mbedtls_test_srv_key,
                         mbedtls_test_srv_key_len, NULL, 0 );
    if( ret != 0 ) {
        EMSG( " failed\n  !  mbedtls_pk_parse_key returned %d\n\n", ret );
        goto exit;
    }

    IMSG( " ok\n" );

    /*
     * 1b. Seed the random number generator
     */
    IMSG( "  . Seeding the random number generator..." );

    if( ( ret = mbedtls_ctr_drbg_seed( &sw_srv->ctr_drbg, mbedtls_entropy_func, &sw_srv->entropy,
                               (const unsigned char *) pers,
                               strlen( pers ) ) ) != 0 ) {
        EMSG( " failed: mbedtls_ctr_drbg_seed returned -0x%04x\n",
                -ret );
        goto exit;
    }

    IMSG( " ok\n" );

    /*
     * 1c. Prepare SSL configuration
     */
    IMSG( "  . Setting up the SSL data...." );

    if( ( ret = mbedtls_ssl_config_defaults( &sw_srv->conf,
                    MBEDTLS_SSL_IS_SERVER,
                    MBEDTLS_SSL_TRANSPORT_STREAM,
                    MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 ) {
        EMSG( " failed: mbedtls_ssl_config_defaults returned -0x%04x\n",
                -ret );
        goto exit;
    }

    mbedtls_ssl_conf_rng( &sw_srv->conf, mbedtls_ctr_drbg_random, &sw_srv->ctr_drbg );

    mbedtls_ssl_conf_ca_chain( &sw_srv->conf, &sw_srv->cachain, NULL );
    if( ( ret = mbedtls_ssl_conf_own_cert( &sw_srv->conf, &sw_srv->srvcert, &sw_srv->pkey ) ) != 0 ) {
        EMSG( " failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n\n", ret );
        goto exit;
    }

    /* enable optional client certificates -- might change to mandatory */
    mbedtls_ssl_conf_authmode( &sw_srv->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );

    /*
    mbedtls_ssl_conf_dbg( &sw_srv->conf, my_debug, stdout );
    mbedtls_debug_set_threshold( 3 );
    */
    //TODO: warning -- client cert can change over session + use is not encourage by devs
    //mbedtls_ssl_conf_renegotiation( &sw_srv->conf, MBEDTLS_SSL_RENEGOTIATION_ENABLED);

    IMSG( " ok\n" );
    return 0;

exit:
    sw_fini_mbedtls_srv(sw_srv);
    /*
    mbedtls_x509_crt_free( &sw_srv->srvcert );
    mbedtls_pk_free( &sw_srv->pkey );
    mbedtls_ctr_drbg_free( &sw_srv->ctr_drbg );
    mbedtls_entropy_free( &sw_srv->entropy );
    mbedtls_ssl_config_free( &sw_srv->conf );
    */
    return( ret );
}

int sw_fini_mbedtls_srv(sw_srv_t *sw_srv) {
    mbedtls_x509_crt_free( &sw_srv->srvcert );
    mbedtls_pk_free( &sw_srv->pkey );
    mbedtls_ctr_drbg_free( &sw_srv->ctr_drbg );
    mbedtls_entropy_free( &sw_srv->entropy );
    mbedtls_ssl_config_free( &sw_srv->conf );
    return 0;
}


/* **************** */
/* CLIENT FUNCTIONS */
/* **************** */
int sw_init_mbedtls_cli(sw_cli_t *sw_cli) {
    int ret;

    if (sw_cli == NULL) return -1;

    /* Make sure memory references are valid */
    mbedtls_ssl_init( &sw_cli->ssl );

    // Get the SSL context ready (note: does NOT copy config)
    if( ( ret = mbedtls_ssl_setup( &sw_cli->ssl, sw_cli->config ) ) != 0 ) {
        EMSG( "  failed: mbedtls_ssl_setup returned -0x%04x\n", -ret );
        goto thread_exit;
    }

    // Note: 2nd argument is user-data passed to the CBs (as "ctx *")
    // non-blocking scenario
    mbedtls_ssl_set_bio( &sw_cli->ssl, sw_cli,
        bstgw_mbedtls_net_send,
        bstgw_mbedtls_net_recv,
        NULL );

    return 0;

thread_exit:
    // TODO: mbedtls_net_free( client_fd );
    mbedtls_ssl_free( &sw_cli->ssl );

    // TODO: thread_info->thread_complete = 1;

    return -1;
}

int sw_fini_mbedtls_cli(sw_cli_t *sw_cli) {
    if (sw_cli == NULL) return -1;
    mbedtls_ssl_free( &sw_cli->ssl );
    return 0;
}


/* ************* */
/* BIO FUNCTIONS */
/* ************* */
int init_flt_buffer(sw_fltbuf_t *fbuf) {
    if (fbuf == NULL) return -1;
    fbuf->next = 0;
    return 0;
}

int free_flt_buffer(sw_fltbuf_t *fbuf) {
    if (fbuf == NULL) return -1;
    return 0;
}

int spaceleft_flt_buffer(const sw_fltbuf_t *fbuf) {
    if (fbuf == NULL) return -1;
    return sizeof(fbuf->buffer) - fbuf->next;
}

int bstgw_mbedtls_net_send( void *ctx, const unsigned char *buf, size_t len ) {
    sw_cli_t *sw_cli = ctx;
    assert(sw_cli != NULL && sw_cli->tx_buf_ptr != NULL);
    // return( MBEDTLS_ERR_NET_INVALID_CONTEXT );

    // tested
    if (len == 0) return 0; // would need errno to detect error / close?!
    if (buf == NULL) return( MBEDTLS_ERR_NET_SEND_FAILED );

    // tx buffer full ("would block")
    if (sw_cli->tx_buf_ptr->full) {
        return( MBEDTLS_ERR_SSL_WANT_WRITE );
    }

    return put_sw_buffer(sw_cli->tx_buf_ptr, buf, len);
}

int bstgw_mbedtls_net_recv( void *ctx, unsigned char *buf, size_t len ) {
    sw_cli_t *sw_cli = ctx;
    assert(sw_cli != NULL && sw_cli->rx_buf_ptr != NULL);

    if (len == 0) return 0; // would need errno to detect error / close?!
    if (buf == NULL) return( MBEDTLS_ERR_NET_RECV_FAILED );

    // rx buffer empty ("would block")
    if (dataleft_sw_buffer(sw_cli->rx_buf_ptr) == 0) {
        return( MBEDTLS_ERR_SSL_WANT_READ );
    }

    return get_sw_buffer(sw_cli->rx_buf_ptr, buf, len);
}