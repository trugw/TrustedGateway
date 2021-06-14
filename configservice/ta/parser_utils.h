#ifndef BSTGW_PARSER_UTILS_H
#define BSTGW_PARSER_UTILS_H

#include "picohttpparser.h"

struct parsing_info {
    const char *method, *path;
    int minor_version;
    struct phr_header headers[20];
    size_t method_len, path_len, num_headers;
    ssize_t nparsed;
    const void *body;
    size_t body_len;
};

void print_http_req_info(struct parsing_info *);
int has_csrf_header(struct parsing_info *);
int has_body(struct parsing_info *);
int has_expected_content_type(struct parsing_info *, const char *);
    
#endif /* BSTGW_PARSER_UTILS_H */