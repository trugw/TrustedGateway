#include "parser_utils.h"

#include <string.h>
#include <tee_internal_api.h> // *MSG()

static const char *csrf_hdr = "TruGW-no-csrf";

int has_csrf_header(struct parsing_info *pinfo) {
    int found = 0;
    for (size_t i = 0; i != pinfo->num_headers; ++i) {
        if ( (strlen(csrf_hdr) == pinfo->headers[i].name_len) &&
            (memcmp(csrf_hdr, pinfo->headers[i].name, strlen(csrf_hdr)) == 0) ) {
            found = 1;
            break;
        }
    }
    return found;
}

int has_body(struct parsing_info *pinfo) {
    if (pinfo->body == NULL || pinfo->body_len <= 0) return 0;
    return 1;
}

int has_expected_content_type(struct parsing_info *pinfo, const char *exp_type) {
    struct phr_header *ct_type = NULL;
    for (size_t i = 0; i != pinfo->num_headers; ++i) {
        if ( (strlen("Content-Type") == pinfo->headers[i].name_len) &&
            (memcmp("Content-Type", pinfo->headers[i].name, strlen("Content-Type")) == 0) )
        {
            ct_type = &pinfo->headers[i];
            break;
        }
    }
    // check if expected content type
    if (ct_type == NULL ||
        ct_type->value_len != strlen(exp_type) ||
        memcmp(ct_type->value, exp_type, ct_type->value_len) != 0) {
        return 0;
    }
    return 1;
}

void print_http_req_info(struct parsing_info *pinfo) {
    if (pinfo == NULL) return;
    DMSG("request is %zd bytes long\n", pinfo->nparsed);
    DMSG("method is %.*s\n", (int)pinfo->method_len, pinfo->method);
    DMSG("path is %.*s\n", (int)pinfo->path_len, pinfo->path);
    DMSG("HTTP version is 1.%d\n", pinfo->minor_version);
    DMSG("headers:\n");
    for (size_t i = 0; i != pinfo->num_headers; ++i) {
        DMSG("%.*s: %.*s\n", (int)pinfo->headers[i].name_len, pinfo->headers[i].name,
            (int)pinfo->headers[i].value_len, pinfo->headers[i].value);
    }
}
