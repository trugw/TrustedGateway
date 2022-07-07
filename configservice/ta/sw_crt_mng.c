#include "sw_crt_mng.h"
#include "response_queue.h"

#include <string.h>
#include <stdlib.h> // abort()

#include <mbedtls/ssl.h>
#include <mbedtls/base64.h>

//#define HAVE_DER_NOCOPY

#ifdef HAVE_DER_NOCOPY
#include <mbedtls/x509_crt.h> // mbedtls_x509_crt_parse_der_nocopy()
#endif

#include <mbedtls/certs.h>

#include <tee_internal_api.h> // *MSG()

// TODO: cleanup on shutdown
static mbedtls_x509_crt master;
static int master_loaded = 0;

static admin_crt_t *allowlist;

static int load_master_cert(void);
static const mbedtls_x509_crt *get_master_cert(void);

#ifndef HAVE_DER_NOCOPY

// TODO: replace with DER and use _nocopy()
// TODO: long-term, pinning the public key might be easier? (but what about signing then?!)
// TODO: add own cert here
#define TEST_MASTER_CERT_PEM                                               \
    "-----BEGIN CERTIFICATE-----\n"                                        \
    "-----END CERTIFICATE-----\n"

static const char bstgw_test_master_crt_rsa_pem[]  = TEST_MASTER_CERT_PEM;
static const size_t bstgw_test_master_crt_rsa_pem_len = sizeof( bstgw_test_master_crt_rsa_pem );

#else

// TODO: add your master cert as byte array
static const uint8_t bstgw_test_master_crt_rsa_der[] = { 0x00, };
static const size_t bstgw_test_master_crt_rsa_der_len = sizeof( bstgw_test_master_crt_rsa_der );

#endif

static int load_master_cert(void) {
    if (master_loaded) return -1;
//    return mbedtls_x509_crt_parse_file(&master, "../keys/mbed-master.crt");

#ifndef HAVE_DER_NOCOPY
    int ret = mbedtls_x509_crt_parse(&master, (const unsigned char *) bstgw_test_master_crt_rsa_pem,
        bstgw_test_master_crt_rsa_pem_len);
#else
    int ret = mbedtls_x509_crt_parse_der_nocopy(&master, bstgw_test_master_crt_rsa_der,
        bstgw_test_master_crt_rsa_der_len);
#endif

    if (ret == 0) master_loaded = 1; // otherwise memory leak!
    return ret;
}

static const mbedtls_x509_crt *get_master_cert(void) {
    int ret;
    if (!master_loaded)
        if( (ret = load_master_cert()) != 0 ) {
            EMSG("Loading master cert failed: -0x%x\n", -ret);
            return NULL;
        }
    return &master;
}

enum client_type get_cert_type(const mbedtls_x509_crt *crt) {
    if (crt == NULL) return INVALID;
    
    /* Master certificate? */
    const mbedtls_x509_crt *mstr = get_master_cert();
    if (mstr == NULL) {
        EMSG("Failed getting the master certificate!\n");
        abort();
    }

    // TODO: use hash instead?
    if ( (crt->raw.len == mstr->raw.len)
        && (memcmp(crt->raw.p, mstr->raw.p, crt->raw.len) == 0))
        return MASTER;
    
    /* Enabled or Pending/Disabled admin? */
    const admin_crt_t *p = NULL;
    for (p = allowlist; p != NULL; p = p->next) {
        // TODO: use hash instead?
        if ( (crt->raw.len == p->crt.raw.len)
            && (memcmp(crt->raw.p, p->crt.raw.p, p->crt.raw.len) == 0) )
            return ADMIN;
    }
    return PENDING;
}

int allow_admin_cert(admin_crt_t *crt) {
    if (crt == NULL) return -1;
    if (allowlist == NULL) { allowlist = crt; return 0; }
    admin_crt_t *c;
    for( c = allowlist; c->next != NULL; c = c->next);
    c->next = crt;
    return 0;
}

struct resp_chunk *get_adminlist(int *len_out) {
    if (len_out == NULL) return NULL;
    struct resp_chunk *list = NULL;
    *len_out = 0;
    
    for(const admin_crt_t *c = allowlist; c != NULL; c = c->next) {
        // DER: c->crt.raw
        struct resp_chunk *crt = malloc(sizeof(struct resp_chunk));
        if (crt == NULL) break; // TODO
        crt->offset = 0;
        crt->next = NULL;

        size_t mem_len = c->crt.raw.len * 4 / 2;
        crt->buf = malloc(crt->len);
        if (crt->buf == NULL) { free(crt); break; }

        // DER --> PEM
        if( mbedtls_base64_encode(crt->buf, mem_len, &crt->len, c->crt.raw.p,
            c->crt.raw.len) != 0)
        {
            free(crt->buf);
            free(crt);
            break;
        }
        crt->take_ownership = 1; // for free()
        append_resp_queue(&list, crt);
        *len_out += crt->len;
    }
    return list;
}

void print_user_type(enum client_type type) {
    switch (type) {
        case USER: IMSG("**USER** mode\n"); break;
        case ADMIN: IMSG("**ADMIN** mode\n"); break;
        case MASTER: IMSG("**MASTER** mode\n"); break;
        case PENDING: IMSG("**_pending_** admin waiting for acceptance\n"); break;
        case INVALID: EMSG("!_invalid_! mode (bug)\n");
        /* fallthrough */
        default: abort();
    }
}