#ifndef BSTGW_SW_FILEIO_H
#define BSTGW_SW_FILEIO_H

#include "sw_crt_mng.h"

char *get_web_app(enum client_type);

int sw_init_webapp_file(const void *file_buffer, uint32_t file_len, uint32_t file_id,
                        const void *sign_buffer, uint32_t sign_len);

void cleanup_webapps(void);

#endif /* BSTGW_SW_FILEIO_H */