#include "client.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h> // memset()

#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <assert.h>

#include <mbedtls/net_sockets.h> // MBEDTLS_ERR_SSL_WANT_READ, ...

#include <bstgw/web_stub_ta.h>
#include <bstgw/ringbuffer.h>

//#define DEBUG_PRINT

static int call_sw_process_runtime(struct bstgw_http_conn *cli_conn, uint32_t cmd_id);

struct bstgw_http_conn *nw_init_tls_client() {
    struct bstgw_http_conn *cli = malloc(sizeof(struct bstgw_http_conn));
    if (cli == NULL) return cli;
    cli->cli_fd = -1;
    cli->next = NULL;

    /* Init TEE and TA Connections required to init the TA-side TLS client */
    if (TEEC_SUCCESS != TEEC_InitializeContext(NULL, &cli->ctx)) goto CliInitErr;

    /* Setup "shared" (TODO: what kind of sharing?! copy-based or paralle access?!) ringbuffers */
    if (init_shm_rb(&cli->ctx, &cli->rx_buffer) != 0) goto RxBufErr;
    if (init_shm_rb(&cli->ctx, &cli->tx_buffer) != 0) goto TxBufErr;

    uint32_t err_origin;
    TEEC_UUID uuid = BSTGW_TA_WEB_STUB_UUID;
    TEEC_Operation op;

    // *client* session type
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].value.a = BSTGW_WEBSTUB_CLIENT_SESSION;

    if (TEEC_SUCCESS != TEEC_OpenSession(&cli->ctx, &cli->sess, &uuid,
        TEEC_LOGIN_PUBLIC, NULL, &op, &err_origin)) {
            printf("Session init error from origin: %u\n", err_origin);
            goto SessionErr;
    }

    /* Now init the TA-side TLS client */
    memset(&op, 0, sizeof(op)); // cleanup before reuse
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&cli->sess, BSTGW_TA_WEB_STUB_CMD_INIT_TLS_CLIENT,
        &op, &err_origin)) {
            printf("Failed to init SW-mbedTLS client (errOrigin: %u)\n", err_origin);
            goto MbedInitErr;
    }

    return cli;

MbedInitErr:
    TEEC_CloseSession(&cli->sess);
SessionErr:
    free_shm_rb(&cli->tx_buffer);
TxBufErr:
    free_shm_rb(&cli->rx_buffer);
RxBufErr:
    TEEC_FinalizeContext(&cli->ctx);
CliInitErr:
    free(cli);
    return NULL;
}

int nw_fini_tls_client(struct bstgw_http_conn *cli) {
    if (cli == NULL) return -1;

    /* Request fini of SW TLS Server */
    uint32_t err_origin;
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&cli->sess, BSTGW_TA_WEB_STUB_CMD_FINI_TLS_CLIENT,
        &op, &err_origin)) {
        printf("WARNING: Failed to fini SW TLS Client (errOrigin: %u)\n", err_origin);
    }

    TEEC_CloseSession(&cli->sess);
    free_shm_rb(&cli->tx_buffer);
    free_shm_rb(&cli->rx_buffer);
    TEEC_FinalizeContext(&cli->ctx);
    free(cli);
    return 0;
}

void *nw_thread_client_conn(void *data) {
    if (data == NULL) return NULL;
    nw_handle_client_conn(data);
    return NULL;
}

static int nw_flush_tx_data(struct bstgw_http_conn *);
static int nw_push_rx_data(struct bstgw_http_conn *);
static int nw_do_handshake_cli(struct bstgw_http_conn *);
static int nw_process_https_requests(struct bstgw_http_conn *);

static int nw_flush_tx_data(struct bstgw_http_conn *cli_conn) {
    char buf[2048]; // make it == tx size buffer
    int blen = sizeof(buf);

    // TODO: if buf == tx size buffer, can just read everything (if buf <, then would need chunking again ...! -- let nw kernel handle that at tcp layer)
    int dataleft =  dataleft_shm_rb(&cli_conn->tx_buffer);
#ifdef DEBUG_PRINT
    printf("Flush dataleft: %d\n", dataleft);
#endif
    if (dataleft < blen) blen = dataleft;
    assert(dataleft <= blen);
    assert(dataleft > 0); // otherwise WANT_WRITE would mean what?!

    // TODO: must be passed via NW-SW API instead (shared buffers! - best would be permanent ones; can demo with temporary ones)
    int ret = get_shm_rb(&cli_conn->tx_buffer, buf, blen);
    if (ret != blen) {
        printf("Failed to read from SW buffer / inconsistent dataleft value\n");
        printf("Got %d, but should have been %d\n", ret, blen);
        return -1; // TODO
    }

    int rest = ret;
    const char *p = buf;
    while ( rest > 0 ) {
        int nsent;
        if ((nsent = send(cli_conn->cli_fd, p, rest, 0)) <= 0) {
            //if (nsent < 0 && errno == EINTR) continue; // TODO: ctrl+C stop
            return nsent;
        }
        rest -= nsent;
        p += nsent;
    }

#ifdef DEBUG_PRINT
    printf("After flush dataleft: %d\n", dataleft_shm_rb(&cli_conn->tx_buffer));
#endif
    return ret;
}

static int nw_push_rx_data(struct bstgw_http_conn *cli_conn) {
    char buf[1500];
    int blen = sizeof(buf);

    // TODO: simplify: don't read more than tx buffer has space, otherwise we have to buffer AGAIN
    int spaceleft = spaceleft_shm_rb(&cli_conn->rx_buffer);
    if (spaceleft < blen) blen = spaceleft;
    assert(spaceleft > 0); // otherwise WANT_READ would mean sw-buffer too small?!

    int nread;
    //while ((nread = recv(cli_conn->cli_fd, buf, blen, 0)) < 0 && errno == EINTR);  // TODO: ctrl+C stop
    nread = recv(cli_conn->cli_fd, buf, blen, 0);
    if (nread <= 0) {
        //assert(nread == 0 || errno != EINTR);
        return nread;
    }

    // TODO: must be passed via NW-SW API instead (shared buffers! - best would be permanent ones; can demo with temporary ones)
    int ret = put_shm_rb(&cli_conn->rx_buffer, buf, nread);
    if (ret != nread) {
        printf("Failed to write to SW buffer / inconsistent spaceleft value\n");
        printf("Put %d, but should have been %d\n", ret, nread);
        return -1; // TODO
    }

    return ret;
}

// TODO: NW-SW buffers must be split and parts of the NW-SW API
static int nw_do_handshake_cli(struct bstgw_http_conn *cli_conn) {
    if (cli_conn == NULL) return -1;
    int sw_ret = -1;
    
    while ((sw_ret = call_sw_process_runtime(cli_conn, BSTGW_TA_WEB_STUB_CMD_DO_HANDSHAKE)) != 0) {
        if (sw_ret < 0 && sw_ret != MBEDTLS_ERR_SSL_WANT_WRITE
            && sw_ret != MBEDTLS_ERR_SSL_WANT_READ) {
            printf("Handshake returned: -0x%x (MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE: -0x%x)\n", -sw_ret, -MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE);
            return -1;
        }

        // check whether we have to flush tx data
        if (dataleft_shm_rb(&cli_conn->tx_buffer) > 0) {
#ifdef DEBUG_PRINT
            printf("flush tx\n");
#endif
            int ret;
            if ((ret = nw_flush_tx_data(cli_conn)) <= 0) {
                if (ret == 0) {
                    printf("Peer closed socket\n");
                    return -1;
                }
                printf("Error on data flush: %d\n", ret);
                return -1;
            }
        }  else {
#ifdef DEBUG_PRINT
            printf("dataleft returned: %d\n", dataleft_shm_rb(&cli_conn->tx_buffer));
#endif
        }


        switch (sw_ret) {
            case MBEDTLS_ERR_SSL_WANT_WRITE: {
#ifdef DEBUG_PRINT
                printf("MBEDTLS_ERR_SSL_WANT_WRITE\n");
#endif
                break; // already flushed above
            }

            case MBEDTLS_ERR_SSL_WANT_READ: {
#ifdef DEBUG_PRINT
                printf("MBEDTLS_ERR_SSL_WANT_READ\n");
#endif
                int ret;
                if ((ret = nw_push_rx_data(cli_conn)) <= 0) {
                    if (ret == 0) {
                        printf("Peer closed socket\n");
                        return -1;
                    }
                    printf("Error on request receive: %d\n", ret);
                    return -1;
                }
                break;
            }

            case 0:
                break;

            default:
                printf("Handshake returned: %x\n", sw_ret);
                return -1;
        }
    }

    printf("Finished client handshake successfully\n");
    return 0;
}

static int nw_process_https_requests(struct bstgw_http_conn *cli_conn) {
    if (cli_conn == NULL) return -1;
    int sw_ret = -1;

    while ((sw_ret = call_sw_process_runtime(cli_conn, BSTGW_TA_WEB_STUB_CMD_PROCESS_REQUEST))) {
        if (sw_ret < 0 && sw_ret != MBEDTLS_ERR_SSL_WANT_WRITE
            && sw_ret != MBEDTLS_ERR_SSL_WANT_READ
            && sw_ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            printf("HTTPS Req Handling returned: -0x%x\n", -sw_ret);
            return sw_ret;
        }

        // check whether we have to flush tx data
        if (dataleft_shm_rb(&cli_conn->tx_buffer) > 0) {
#ifdef DEBUG_PRINT
            printf("flush tx\n");
#endif
            int ret;
            if ((ret = nw_flush_tx_data(cli_conn)) < 0) {
                if (ret == 0) {
                    printf("Peer closed socket\n");
                    return 0;
                }
                printf("Error on data flush: %d\n", ret);
                return -1;
                // TODO: == 0 --> inform SW + break out of while loop
            }
        } else {
#ifdef DEBUG_PRINT
            printf("dataleft returned: %d\n", dataleft_shm_rb(&cli_conn->tx_buffer));
#endif
        }

        switch (sw_ret) {
            case MBEDTLS_ERR_SSL_WANT_WRITE: {
#ifdef DEBUG_PRINT
                printf("MBEDTLS_ERR_SSL_WANT_WRITE\n");
#endif
                break; // already flushed above
            }

            case MBEDTLS_ERR_SSL_WANT_READ: {
#ifdef DEBUG_PRINT
                printf("MBEDTLS_ERR_SSL_WANT_READ\n");
#endif
                int ret;
                if ((ret = nw_push_rx_data(cli_conn)) <= 0) {
                    if (ret == 0) {
                        printf("Peer closed socket\n");
                        return 0;
                    }
                    printf("Error on request receive: %d\n", ret);
                    return -1;
                    // TODO: == 0 --> inform SW + break out of while loop
                }
                break;
            }

            case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY: {
                printf("Graecful connection close by peer\n");
                return 0;
            }

            case 1:
                break; // ? (TODO: will not be reached at the moment)

            default:
                printf("sw_read_https_req_data returned: -0x%x\n", -sw_ret);
                return -1;
        }
    }

    return 0;
}

static int nw_init_http_cli(struct bstgw_http_conn *cli_conn);
static int nw_fini_http_cli(struct bstgw_http_conn *cli_conn);

int nw_handle_client_conn(struct bstgw_http_conn *cli_conn) {
    printf("-----------\nWelcome to the client handler thread\n");

    if (nw_do_handshake_cli(cli_conn) < 0) {
        printf("Client: failed to perform handshake\n");
        close(cli_conn->cli_fd);
        return -1;
    }

    // TODO: consider removing here, and directly calling in SW on handshake finish
    if (nw_init_http_cli(cli_conn) < 0) {  // TODO: move?
        printf("Client: failed setting up HTTP client\n");
        close(cli_conn->cli_fd);
        return -1;
    }

    int ret = nw_process_https_requests(cli_conn);
    if (ret < 0) {
        printf("Request handling stopped with an error: %d\n", ret);
    }

    // TODO: shutdown process
    // TODO: consider dropping and making it part of SW TLS Cli cleanup
    //       (or merge both into a SW HTTPS client)
    nw_fini_http_cli(cli_conn); // TODO: move?
    close(cli_conn->cli_fd);
    return 0;
}

static int nw_init_http_cli(struct bstgw_http_conn *cli_conn) {
    if (cli_conn == NULL) return -1;
    uint32_t err_origin;

    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&cli_conn->sess, BSTGW_TA_WEB_STUB_CMD_INIT_HTTP_CLI,
        &op, &err_origin)) {
            printf("Failed to init SW HTTP client (errOrigin: %u)\n", err_origin);
            return -1;
    }
    return 0;
}

static int nw_fini_http_cli(struct bstgw_http_conn *cli_conn) {
    if (cli_conn == NULL) return -1;
    uint32_t err_origin;
    
    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    if (TEEC_SUCCESS != TEEC_InvokeCommand(&cli_conn->sess, BSTGW_TA_WEB_STUB_CMD_FINI_HTTP_CLI,
        &op, &err_origin)) {
            printf("Failed to fini SW HTTP client (errOrigin: %u)\n", err_origin);
            return -1;
    }
    return 0;
}

/* Return -1, or mbedTLS return value */
static int call_sw_process_runtime(struct bstgw_http_conn *cli_conn, uint32_t cmd_id) {
    if (cli_conn == NULL) return -0x1337;

    TEEC_Result tee_ret;
    uint32_t err_origin;

    TEEC_Operation op;
    memset(&op, 0, sizeof(op));
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_VALUE_OUTPUT, // extra return value
		TEEC_MEMREF_WHOLE, // rx ringbuffer
		TEEC_MEMREF_WHOLE, // tx ringbuffer
        TEEC_NONE);
    // alternative: TEEC_MEMREF_PARTIAL_INOUT/INPUT/OUTPUT

    // whole, so just have to set parent
    op.params[1].memref.parent = &cli_conn->rx_buffer.shm;
    op.params[2].memref.parent = &cli_conn->tx_buffer.shm;

    tee_ret = TEEC_InvokeCommand(&cli_conn->sess, cmd_id, &op, &err_origin);
    if (tee_ret != TEEC_SUCCESS) return -0x4711; // TODO: own error value?!

    // TODO: size value of shared memory MAY be changed?!

    // TODO: would have to parse different if 32 Bit, bcs. mbedTLS uses "int" as return value
    return op.params[0].value.a;
}