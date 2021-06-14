#ifndef BSTGW_SW_CRTMNG_H
#define BSTGW_SW_CRTMNG_H

#include "secure_ta.h"
#include <mbedtls/x509_crt.h>

#include <bstgw/web_stub_ta.h>

enum client_type get_client_type(struct sw_client_session *);

enum client_type get_cert_type(const mbedtls_x509_crt *);

typedef struct admin_cert {
    mbedtls_x509_crt crt;
    struct admin_cert *next;
} admin_crt_t;

int allow_admin_cert(admin_crt_t *);

struct resp_chunk *get_adminlist(int *lp);

//enum admin_cert_status { DISABLED, ENABLED };

void print_user_type(enum client_type type);

#endif /* BSTGW_SW_CRTMNG_H */