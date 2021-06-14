#include "secure_ta.h"
#include "sw_mbedtls_util.h"

#include "sw_fileio.h" // placeholder for op-tee file i/o

#include "sw_crt_mng.h"

#include "parser_utils.h"

#include "picohttpparser.h"

#include "response_queue.h"

//#include "config_proxy.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <tee_internal_api.h> // *MSG()

//#include <errno.h> // strtol()| TODO: this will probably never ever exist in OP-TEE?! -- cf. strtol()

//#define DEBUG_PRINT
//#define DEBUG_PRINT_REQ
//#define DEBUG_PRINT_RESP

// TODO: Cache result in clip !, bcs. especially the certificate comparison is expensive each time!
enum client_type get_client_type(struct sw_client_session *clip) {
    if (clip == NULL) return INVALID;
    switch (mbedtls_ssl_get_verify_result(&clip->ssl)) {
        case MBEDTLS_X509_BADCERT_MISSING: return USER;
        /* TODO: remove */
        case MBEDTLS_X509_BADCERT_SKIP_VERIFY: return USER;
        /* */
        case 0: {
            /* TODO: if we can move this part in the Cert. verify CB, we could disable
             * KEEP_PEER_CERTIFICATE and thereby gain precious extra RAM !! */
            const mbedtls_x509_crt *adm_crt = mbedtls_ssl_get_peer_cert(&clip->ssl);
            if (adm_crt == NULL) return INVALID;
            return get_cert_type(adm_crt);
        }
        default: return INVALID;
    }
}

static int handle_get_request(struct sw_client_session *, struct parsing_info *);
static int send_http_resp(struct sw_client_session *, int, const char *, size_t, struct resp_chunk *, const char *);
static int send_http_err_resp(struct sw_client_session *, int, const char *);
static int flush_resp_queue(struct sw_client_session *);

/* init TLS part of HTTPS server */
int sw_init_tls_srv(struct sw_server_session **srvp) {
    if (srvp == NULL) return -1;
    if ( (*srvp = malloc(sizeof(sw_srv_t))) == NULL ) return -1;
    if (sw_init_mbedtls_srv(*srvp)) { free(*srvp); return -1; }
    return 0;
}

/* free TLS part of HTTPS server */
int sw_fini_tls_srv(struct sw_server_session *srvp) {
    if (srvp == NULL) return -1;
    sw_fini_mbedtls_srv(srvp);
    free(srvp);
    // TODO: how to do that in a cleaner way, non-global variable way,
    //       w/o making it inavailable for client sessions?
    cleanup_webapps();
    return 0;
}

/* init TLS part of new HTTPS client */
int sw_init_tls_cli(struct sw_client_session **clip,
    const mbedtls_ssl_config *srv_config) {
    if (clip == NULL || srv_config == NULL) return -1;

    // sw client struct
    if ( (*clip = malloc(sizeof(sw_cli_t))) == NULL ) return -1;
    (*clip)->rx_buf_ptr = NULL;
    (*clip)->tx_buf_ptr = NULL;
    /*if (init_sw_buffer(&(*clip)->rx_buffer)) { goto sw_cli_error; }
    if (init_sw_buffer(&(*clip)->tx_buffer)) {
        free_sw_buffer(&(*clip)->rx_buffer);
        goto sw_cli_error;
    }*/
    (*clip)->config = srv_config;

    // init mbedTLS structs
    if (sw_init_mbedtls_cli(*clip)) {
       /* free_sw_buffer(&(*clip)->rx_buffer);
        free_sw_buffer(&(*clip)->tx_buffer);*/
        goto sw_cli_error;
    }
    return 0;

sw_cli_error:
    free(*clip);
    return -1;
}

/* free TLS part of a HTTPS client */
int sw_fini_tls_cli(struct sw_client_session *clip) {
    if (clip == NULL) return -1;
    sw_fini_mbedtls_cli(clip);
    /*
    free_sw_buffer(&clip->rx_buffer);
    free_sw_buffer(&clip->tx_buffer);
    */
    free(clip);
    return 0;
}

const struct mbedtls_ssl_config *get_tls_srv_conf(struct sw_server_session *srvp) {
    if (srvp == NULL) return NULL;
    return &srvp->conf;
}
int set_rxtx_buffers(struct sw_client_session *clip, void *rx, void *tx) {
    if (clip == NULL || rx == NULL || tx == NULL) return -1;
    clip->rx_buf_ptr = rx;
    clip->tx_buf_ptr = tx;
    return 0;
}
int clear_rxtx_buffers(struct sw_client_session *clip) {
    if (clip == NULL) return -1;
    clip->rx_buf_ptr = NULL;
    clip->tx_buf_ptr = NULL;
    return 0;
}

/* init HTTP intermediate buffer */
int sw_init_http_cli(struct sw_client_session *clip) {
    if (clip == NULL) return -1;
    if (init_flt_buffer(&clip->sw_http_cli.http_req_buffer)) return -1;
    clip->sw_http_cli.resp_buffer = NULL;
    clip->sw_http_cli.is_partial_request = 0;
    return 0;
}

/* free HTTP intermediate buffer */
int sw_fini_http_cli(struct sw_client_session *clip) {
    if (clip == NULL) return -1;
    free_flt_buffer(&clip->sw_http_cli.http_req_buffer);
    free_resp_queue(clip->sw_http_cli.resp_buffer);
    return 0;
}

int sw_do_handshake_cli(struct sw_client_session *clip) {
    if (clip == NULL) return -1;
    // TODO: handle mbedTLS errors here already!
    int ret = mbedtls_ssl_handshake(&clip->ssl);

    // If Successful, check if client sent no/a valid/invalid certificate
    if (ret == 0) {
        int vrfy = mbedtls_ssl_get_verify_result(&clip->ssl);
        IMSG("Get Verify result: %d, handshake: %d\n", vrfy, ret);
        switch(vrfy) {
            case 0: {
                IMSG("Client certificate available and signed by correct CA\n");
                break;
            }
            case MBEDTLS_X509_BADCERT_MISSING: {
                IMSG("NO client certificate, i.e., non-admin user\n");
                break;
            }
            case -1: {
                EMSG("No certificate verification result, so probably a pending handshake error\n");
                assert(ret < 0);
                break;
            }
            /* TODO: remove */
            case MBEDTLS_X509_BADCERT_SKIP_VERIFY: {
                DMSG("We currently skip client cert. verification for testing under less memory pressure\n");
                break;
            }
            /* */
            default: {
                EMSG("Invalid client certificate has been presented, abort!\n");
                mbedtls_ssl_close_notify(&clip->ssl); // TODO: currently not sent by client.c
                ret = MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED;
                break;
            }
        }
        print_user_type(get_client_type(clip));
    }
    return ret;
}

/* TODO: is it actually needed? */
int sw_notify_transport_close(struct sw_client_session *clip) {
    (void)clip;
    return 0;
}

/* TODO: is it actually needed? */
int sw_notify_transport_error(struct sw_client_session *clip, int err) {
    (void)err;
    return sw_notify_transport_close(clip);
}


/* ************** */
/* HTTP FUNCTIONS */
/* ************** */
struct req_handler {
    const char *path;
    enum client_type allowed_clients[4];
    size_t allow_cli_len;
    int needs_body;
    const char *exp_content_type;
    int(* handler)(struct sw_client_session *, struct parsing_info *);
};

static const struct req_handler *get_req_handler(const struct req_handler *handler_list,
    size_t list_len, const char *path, size_t path_len) {
    for (size_t i=0; i < list_len; i++) {
        const struct req_handler *h = &handler_list[i];
        if( (strlen(h->path) == path_len)
            && (memcmp(h->path, path, path_len) == 0) ) {
            return h;
        }
    }
    return NULL;
}

static const int RequestIsTooLongError = -3000;

static int handle_get_request(struct sw_client_session *clip, struct parsing_info *pinfo) {
    if (clip == NULL || pinfo == NULL) {
        EMSG("clip == NULL || pinfo == NULL\n");
        abort();
    }
    enum client_type user_type = get_client_type(clip);

    /* Check for valid paths */
    if ((strlen("/") != pinfo->path_len) ||
        (memcmp("/", pinfo->path, pinfo->path_len) != 0)) {
        EMSG("Invalid path requested in GET\n");
        return send_http_err_resp(clip, 404, "Not Found");
    }

    /* Send HTTP response. */
    char *webapp = get_web_app(user_type);
    if (webapp == NULL) {
        EMSG("Did not find web app! (returned NULL)\n");
        return send_http_err_resp(clip, 403, "Forbidden");
    }

    struct resp_chunk *webapp_chunk = pack_into_resp_chunk(webapp, strlen(webapp));
    if (webapp_chunk == NULL) { return -1; }
    int ret = send_http_resp(clip, 200, "OK", strlen(webapp),  webapp_chunk, "text/html");
    return ret;
}

static int dummy_handler(struct sw_client_session *clip, struct parsing_info *pinfo) {
    (void)pinfo;
    return send_http_resp(clip, 200, "OK", 0, NULL, NULL);
}

/* TODO: move into config_proxy.c/h and make send_http_{err_}resp() non-static */
#include <tee_api.h>
#include <bstgw/pta_trustrouter.h>

static int post_npfctl_cmd(struct sw_client_session *clip, struct parsing_info *pinfo) {
    if (clip == NULL || pinfo == NULL || pinfo->body_len == 0) return -1;

    // TODO: what makes sense?! how to handle bigger sizes?
    // Note:  initial "show" CMD already returned a 2.307 Bytes packed nvlist
    //const int MAX_NVLIST_RESP = 4096;
    // small config already resulted in a show of 5.744 B
    // TODO: could it be split somehow?
    const int MAX_NVLIST_RESP = 8192;
    void *resp_buffer = malloc(MAX_NVLIST_RESP);
    if (resp_buffer == NULL) return send_http_err_resp(clip, 500, "Internal Server Error");

	TEE_TASessionHandle sess = TEE_HANDLE_NULL;
    const TEE_UUID core_uuid = BSTGW_PTA_TRUST_ROUTER_UUID;

    uint32_t param_types = TEE_PARAM_TYPES(
        TEE_PARAM_TYPE_VALUE_INPUT, TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE
    );
	TEE_Param params[TEE_NUM_PARAMS] = { };

    /* Create Session */
    params[0].value.a = BSTGW_TRUST_ROUTER_UNBOUND_SESSION;
    if (TEE_OpenTASession(&core_uuid, TEE_TIMEOUT_INFINITE, param_types,
        params, &sess, NULL) != TEE_SUCCESS)
        goto fail;

    /* Prepare Config submission */
    param_types = TEE_PARAM_TYPES(
        TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_OUTPUT,
        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE
    );

	memset(&params, 0, sizeof(params));
	params[0].memref.buffer = pinfo->body;
	params[0].memref.size = pinfo->body_len;

    // TODO: How to handle bigger sizes? Is it possible to let target TA allocate/define the size?
    params[1].memref.buffer = resp_buffer;
    params[1].memref.size = MAX_NVLIST_RESP;

    /* Submit Config request */
	TEE_Result res = TEE_InvokeTACommand(sess, TEE_TIMEOUT_INFINITE,
        BSTGW_PTA_TRUST_ROUTER_CMD_CONFIG,
        param_types, params, NULL);

    /* Close Session */
	TEE_CloseTASession(sess);
	sess = TEE_HANDLE_NULL;
    if (res != TEE_SUCCESS) goto fail;

    /* Prepare response for sending */
    struct resp_chunk *resp = pack_into_resp_chunk(params[1].memref.buffer, params[1].memref.size);
    if (!resp) goto fail;
    resp->take_ownership = 1;

    /* Send config response via HTTP to WebApp */
    return send_http_resp(clip, 200, "OK", params[1].memref.size, resp, "application/octet-stream");

fail:
    free(resp_buffer);
    return send_http_err_resp(clip, 500, "Internal Server Error");
}

static int post_register_admin(struct sw_client_session *clip, struct parsing_info *pinfo) {
    struct admin_cert *crt = malloc(sizeof(struct admin_cert));
    if (crt == NULL) return send_http_err_resp(clip, 500, "Internal Server Error");
    crt->next = NULL;

    mbedtls_x509_crt_init(&crt->crt);
    int ret = mbedtls_x509_crt_parse(&crt->crt, pinfo->body, pinfo->body_len);
    if (ret < 0) {
        EMSG("Failed parsing the cert\n");
        free(crt);
        return send_http_resp(clip, 200, "OK", 4, pack_into_resp_chunk("FAIL", 4), "text/plain");
    }

    if (allow_admin_cert(crt) != 0) {
        EMSG("Failed storing new admin cert\n");
        mbedtls_x509_crt_free(&crt->crt);
        free(crt);
        return send_http_resp(clip, 200, "OK", 4, pack_into_resp_chunk("FAIL", 4), "text/plain");
    }

    return send_http_resp(clip, 200, "OK", 10, pack_into_resp_chunk("REGISTERED", 10), "text/plain");
}

static int post_get_adminlist(struct sw_client_session *clip, struct parsing_info *pinfo) {
    (void)pinfo;
    int list_len;
    struct resp_chunk *admin_list = get_adminlist(&list_len);
    if (admin_list == NULL) return send_http_resp(clip, 200, "OK", 0, NULL, "text/plain");

    int ret = send_http_resp(clip, 200, "OK", list_len, admin_list, "text/plain");
    return ret;
}

/* { path, {allowed_user_types}, #user_types, needs_body, req_type, handler_fct } */
static const struct req_handler post_handler_list[] = {
    { "/trustconf", {ADMIN, MASTER}, 2, 1, "application/octet-stream", post_npfctl_cmd },
    { "/register-admin", {USER}, 1, 1, "text/plain", post_register_admin },
    // TODO: should be a GET; and POST should be for pushing "enable/disable/remove"
    { "/adminlist", {MASTER}, 1, 0, "text/plain", post_get_adminlist },
    // TODO: query netif, route config
    { "trustroute", {ADMIN, MASTER}, 2, 1, "application/octet-stream", dummy_handler }
};
static const size_t post_list_len = sizeof(post_handler_list)/sizeof(post_handler_list[0]);

static int handle_post_request(struct sw_client_session *clip, struct parsing_info *pinfo) {
    IMSG("A POST request\n");

    /* Check for anti-CSRF header */
    if (!has_csrf_header(pinfo)) {
        EMSG("CSRF attempt detected!\n");
        return -1;
    }

    /* Check for valid paths */
    const struct req_handler *hndl;
    if( (hndl = get_req_handler(post_handler_list, post_list_len,
        pinfo->path, pinfo->path_len)) == NULL ) {
        EMSG("Invalid path requested in POST\n");
        return send_http_err_resp(clip, 404, "Not Found");
    }
    
    /* Check if user type has access rights */
    enum client_type user_type = get_client_type(clip);
    int allowed = 0;
    for (size_t i=0; i<hndl->allow_cli_len; i++) {
        if (hndl->allowed_clients[i] == user_type) { allowed = 1; break; }
    }
    if (!allowed) return send_http_err_resp(clip, 403, "Forbidden");

    /* Check that we have a body */
    if(hndl->needs_body && !has_body(pinfo)) {
        EMSG("POST request w/o body\n");
        return send_http_err_resp(clip, 404, "Not Found"); // TODO
    }

    /* Extract and check body's content-type */
    if (!has_expected_content_type(pinfo, hndl->exp_content_type)) {
        EMSG("Missing or unexpected content-type\n");
        return send_http_err_resp(clip, 415, "Unsupported Media Type");
    }

#ifdef DEBUG_PRINT_REQ
    DMSG("POST Body: '%.*s' (body_len: %lu)\n", (int)pinfo->body_len, pinfo->body, pinfo->body_len);
#endif

    return hndl->handler(clip, pinfo);
}

/* 0 == full write done;
   WANT_WRITE == partial write done;
   else == error
*/
static int flush_resp_queue(struct sw_client_session *clip) {
    if (clip == NULL) abort();
    struct sw_http_client *http_cli = &clip->sw_http_cli;
    struct resp_chunk *curr = http_cli->resp_buffer;

    while ( curr != NULL ) {
        int to_sent = curr->len - curr->offset;
        if ((curr->buf != NULL) && (to_sent > 0) ) {
            int nsent = mbedtls_ssl_write(&clip->ssl,
                ((uint8_t *)curr->buf) + curr->offset, to_sent);

            // want_write / error
            if (nsent < 0) {
                DMSG("ssl_write returned < 0: -0x%x\n", -nsent);
                return nsent;
            }

            // partial sent
            if ( nsent != to_sent ) {
                DMSG("Partial write of %d bytes\n", nsent);
                curr->offset += nsent;
                return MBEDTLS_ERR_SSL_WANT_WRITE;
            }

            // TODO: no send?
            if ( nsent == 0 ) {
                EMSG("ssl_write returned 0\n");
                abort();
            }
        }
        curr = curr->next;
        delete_head_resp_queue(&http_cli->resp_buffer);
    }

    return 0;
}

// move at front (if not already) to allow appending new data
// if ((bp != req_buf) && (data_len > 0)) {
#define MOVE_REST_TO_BUF_HEAD \
    if (bp != req_buf) { \
        DMSG("memmove\n"); \
        memmove(req_buf, bp, data_len); \
        *req_idx = data_len; \
        bp = req_buf; \
    } else { \
        DMSG("memmove w/o action, don't we have to still do sth.?!\n"); \
    }

//const char *http_resp_tmplt = "HTTP/1.1 %d %s\r\nContent-Length: %ld\r\nContentType: %s\r\n\r\n%.*s";
//#define http_resp_tmplt ("HTTP/1.1 %d %s\r\nContent-Length: %ld\r\nContentType: %s\r\n\r\n%.*s")

#define http_resp_tmplt ("HTTP/1.1 %d %s\r\nContent-Length: %ld\r\nContentType: %s\r\n\r\n")

// TODO: do NOT send ContentType if length is 0
static int send_http_resp(struct sw_client_session *clip, int code, const char *status,
    size_t datalen, struct resp_chunk *body_chunk, const char *data_type) {
    if (clip == NULL) abort();
    size_t outlen = strlen(http_resp_tmplt)-12 + 100;
#ifdef DEBUG_PRINT_RESP
    DMSG("outlen: %ld\n", outlen);
#endif
    char *outbuf = malloc(outlen);
    if (outbuf == NULL) {
        EMSG("Out of memory for out buffer\n");
        return -1;
    }

    char *type = (char *)"";
    size_t blen = 0;
    if (body_chunk != NULL && datalen > 0 && type != NULL) {
        blen = datalen;
        type = (char *)data_type;
    }

    int nsent = 0;
    if(( nsent = snprintf(outbuf, outlen, http_resp_tmplt, code, status, blen, type)) < 0) {
        EMSG("Error while trying to craft response");
        free(outbuf);
        return -1;
    }
#ifdef DEBUG_PRINT_RESP
    DMSG("reponse len: %d\n'%.*s'\n", nsent, nsent, outbuf);
    DMSG("data len: %lu\n", blen);
#endif

    struct resp_chunk *hdr_chunk = pack_into_resp_chunk(outbuf, nsent);
    if (hdr_chunk == NULL) {
        EMSG("Out of memory while trying to pack header chunk!\n");
        free(outbuf);
        return -1;
    }
    hdr_chunk->take_ownership = 1; // for freeing

    DMSG("Append response chunks and flush\n");

    // queue header and body, then flush (send)
    append_resp_queue(&clip->sw_http_cli.resp_buffer, hdr_chunk);
    // NOTE: append does nothing if NULL is passed as new chunk
    append_resp_queue(&clip->sw_http_cli.resp_buffer, body_chunk);
    return flush_resp_queue(clip);
}

static int send_http_err_resp(struct sw_client_session *clip, int err, const char *msg) {
    if (err == 204 || err == 304 || (100 <= err && err <= 199)) abort(); // no content-length
    return send_http_resp(clip, err, msg, 0, NULL, NULL);
}

int sw_read_https_req_data(struct sw_client_session *clip) {
    if (clip == NULL) return -1000;
    struct sw_http_client * const http_cli = &clip->sw_http_cli;
    int ret = -1;

    if (get_client_type(clip) == INVALID) {
        EMSG("INVALID client type (malicious call-order by normal world?!)\n");
        return -1;
    }

    /* Try to ssl_write pending HTTP Response data ! */
    if (http_cli->resp_buffer != NULL) {
#ifdef DEBUG_PRINT_RESP
        DMSG("Will send pending HTTP response data\n");
#endif
        // note: will put unwritten data back to response buffer
        if ((ret = flush_resp_queue(clip)) != 0) {
            return ret;
        }
        assert(http_cli->resp_buffer == NULL);
    }

    char *req_buf = http_cli->http_req_buffer.buffer;
    uint32_t *req_idx = &http_cli->http_req_buffer.next;

    char *bp = (char *)req_buf;
    size_t data_len = *req_idx;

    while (1) {
#ifdef DEBUG_PRINT
        DMSG("Enter receive loop\n");
#endif

        // TODO: should make inner + outer loop, where the if body is in outer, and all following (which is only working on data_len) is in inner loop;

        /* If no or only partial data buffered, receive new data */
        //if ( (dataleft_flt_buffer(&http_cli->http_req_buffer) == 0) ||
        if ( (data_len == 0) || http_cli->is_partial_request) {
            if (data_len != *req_idx || bp != req_buf) {
                EMSG("data_len != *req_idx || bp != req_buf\n");
                abort();
            }

#ifdef DEBUG_PRINT
            DMSG("req_idx: %d, spaceleft: %lu\n", *req_idx,
                (sizeof(http_cli->http_req_buffer.buffer) - *req_idx));
#endif

            /* Try to decrypt new request data. */
            // TODO: handle == 0?
            // TODO: data_len instead of *req_idx ?!
            if( (ret = mbedtls_ssl_read(&clip->ssl, (uint8_t*) &req_buf[*req_idx],
                                        (sizeof(http_cli->http_req_buffer.buffer) - *req_idx))) <= 0 ) {

#ifdef DEBUG_PRINT
                DMSG("mbedtls_ssl_read: -0x%x (WANT_READ: -0x%x)\n", -ret, -MBEDTLS_ERR_SSL_WANT_READ);
#endif

                // confirm graceful shutdown
                if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
                    mbedtls_ssl_close_notify(&clip->ssl);
                return ret;
            }
            *req_idx += ret; // new data buffered

            // update working set
            bp = req_buf;
            data_len = *req_idx;
        }

#ifdef DEBUG_PRINT
        if (data_len != 0) DMSG("data_len != 0");
        if (!http_cli->is_partial_request) DMSG("!is_partial_request");
#endif

        /* Try to parse HTTP request. */
        struct parsing_info prs_info;
        prs_info.num_headers = sizeof(prs_info.headers) / sizeof(prs_info.headers[0]);

        size_t prevlen = 0; // TODO

        // parse
#ifdef DEBUG_PRINT
        DMSG("Will issue parse function\n");
#endif
        prs_info.nparsed = phr_parse_request(bp, data_len,
            &prs_info.method, &prs_info.method_len,
            &prs_info.path, &prs_info.path_len,
            &prs_info.minor_version,
            prs_info.headers, &prs_info.num_headers,
            prevlen);
    
        // TODO: how to recover?
        if (prs_info.nparsed == -1) {
            // TODO: How much of the buffer should be dropped ?! -- if we don't drop anything, we might loop here forever
            if ((ret = send_http_err_resp(clip, 400, "Bad Request")) != 0) {
                // TODO: don't know how to recover, so return error anyway
                //return ret;
            }
            // TODO: I 't know how to find out how much data to throw away, so try to stop gracefully, but don't re-loop for it
            mbedtls_ssl_close_notify(&clip->ssl);
            return -1;
        }
    
        /* request is incomplete, continue the loop */
        if (prs_info.nparsed == -2) {
            IMSG("Note: incomplete request\n");
            //return RequestIsTooLongError;
            // move at front (if not already) to allow appending new data
            MOVE_REST_TO_BUF_HEAD
            http_cli->is_partial_request = 1;
            continue;
        }

        /* successfully parsed the request */
        assert(prs_info.nparsed > 0);
#ifdef DEBUG_PRINT_REQ
        DMSG("Successfully parsed HTTP request (headers)\n");
        print_http_req_info(&prs_info);
        DMSG("unused buffer data: %zd bytes\n", data_len - prs_info.nparsed);
#endif

        /* scan for Content-Length to find out whether we have to receive the body as well */
        prs_info.body = NULL;
        prs_info.body_len = 0;
        for (size_t i = 0; i != prs_info.num_headers; ++i) {
            if ( (strlen("Content-Length") == prs_info.headers[i].name_len) &&
                (memcmp("Content-Length", prs_info.headers[i].name, strlen("Content-Length")) == 0) ) {
#ifdef DEBUG_PRINT_REQ
                DMSG("Content-Length specified! Have to parse body.\n");
#endif
                prs_info.body = bp + prs_info.nparsed;
                
                // parse content-length value (it's a decimal number in text format)
                char buf[11];
                if((sizeof(buf)-1) < prs_info.headers[i].value_len) {
                    EMSG("(sizeof(buf)-1) < prs_info.headers[i].value_len\n");
                    abort();
                }
                memcpy(buf, prs_info.headers[i].value, prs_info.headers[i].value_len);
                buf[prs_info.headers[i].value_len] = '\0';

                /* TODO: strtol() not directly available (cf. our newlib import) ++ no errno available */
                /*errno = 0;*/
                //prs_info.body_len = strtol(buf, (char **)NULL, 10);
                prs_info.body_len = strtoul(buf, (char **)NULL, 10);
                

                // note: Can return 0 if no digits at all, or ULONG_MAX if overflow stuff
                // TODO: errno value needed to distinguish 0 (success) from 0 (error), but not available in OP-TEE
                if (prs_info.body_len == ULONG_MAX
                    || ((prs_info.body_len == 0) && (memcmp("0", buf, 1) != 0)) ) {
                    EMSG("Error parsing content-length number\n");
                    return -1;
                }
                break;
            }
        }

        /* TODO: check if we miss body data */
        if (prs_info.body != NULL) {
            // bad request:  this is an illegal value
            //if (prs_info.body_len < 0) { ... }

            // body data, but not all in buffer
            if (prs_info.body_len > 0 &&
                ((data_len - prs_info.nparsed) < prs_info.body_len) ) {

                // request too large (OOM)
                if ( sizeof(http_cli->http_req_buffer.buffer) < (data_len + prs_info.body_len) ) {
                    EMSG("Error: request with Body would not fit into HTTP request buffer (buffer: %u, req: %u)\n", 
                    sizeof(http_cli->http_req_buffer.buffer), data_len + prs_info.body_len);
                    return RequestIsTooLongError;
                }

#ifdef DEBUG_PRINT_REQ
                DMSG("Seems we are mssing missing parts of the body\n");
                DMSG("current: %ld, parsed: %ld, body_len: %ld\n", data_len, prs_info.nparsed, prs_info.body_len);
#endif

                http_cli->is_partial_request = 1;
                MOVE_REST_TO_BUF_HEAD
                continue;
            }
        }


        /* Handle HTTP request. */

        // update for next round (or breaks below)
        // TODO: we don't update idx which is bad, but updating it is only correct combined with a memmove()
        // TODO: should make inner + outer loop
        http_cli->is_partial_request = 0;
        bp = req_buf + (prs_info.nparsed + prs_info.body_len);
        data_len -= (prs_info.nparsed + prs_info.body_len);

#ifdef DEBUG_PRINT
        DMSG("Remaining data_len: %zd bytes\n", data_len);
#endif

        /* GET */
        if ((strlen("GET") == prs_info.method_len) &&
            (memcmp("GET", prs_info.method, prs_info.method_len) == 0)) {
            
            if ((ret = handle_get_request(clip, &prs_info)) != 0) {
                IMSG("handle GET returned: -0x%x\n", -ret);
                // TODO: could just do in case of WANT_WRITE, bcs. all others stop atm
                MOVE_REST_TO_BUF_HEAD
                return ret;
            }
        /* POST */
        } else if ((strlen("POST") == prs_info.method_len) &&
            (memcmp("POST", prs_info.method, prs_info.method_len) == 0)) {

            if ((ret = handle_post_request(clip, &prs_info)) != 0) {
                IMSG("handle POST returned: %d\n", ret);
                // TODO: could just do in case of WANT_WRITE, bcs. all others stop atm
                MOVE_REST_TO_BUF_HEAD
                return ret;
            }

        // TODO: The two mandatory methods, GET and *HEAD*, must never be disabled and should not return this error code.

        } else {
            EMSG("Unsupported request!\n");

            // TODO: "The server MUST generate an Allow header field in a 405 response containing a list of the target resource's currently supported methods."
            if ((ret = send_http_err_resp(clip, 405, "Method Not Allowed")) != 0) {
                // TODO: could just do in case of WANT_WRITE, bcs. all others stop atm
                MOVE_REST_TO_BUF_HEAD
                return ret;
            }
        }

        // Note:  if we end up here, we either have no rest data, or unparsed data
        if (data_len == 0) {
            // no rest data
            bp = req_buf;
            *req_idx = 0;
        }

    // Note: could also use while(true) instead, bcs. it will break with WANT_READ (also flushed tx), or will at some point fill the resp. buffer (with valid replies or errors) causing a break with WANT_WRITE
    } //while((*lp - nparsed) > 0);


    return 1; // TODO: 0 would mean connection got closed? [actually there is a different return value indicating a TLS PEER CLOSE, right?]
}
