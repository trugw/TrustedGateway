#ifndef BSTGW_SHM_RB_H
#define BSTGW_SHM_RB_H

#include <bstgw/ringbuffer.h>
#include <tee_client_api.h>

typedef struct {
    TEEC_SharedMemory shm;
    sw_buff_t *rb;
} shm_rb_t;

TEEC_Result init_shm_rb(TEEC_Context *, shm_rb_t *);
TEEC_Result free_shm_rb(shm_rb_t *);
int put_shm_rb(shm_rb_t *, const void *, size_t);
int get_shm_rb(shm_rb_t *, void *, size_t);
int spaceleft_shm_rb(const shm_rb_t *);
int dataleft_shm_rb(const shm_rb_t *);

#endif /* BSTGW_SHM_RB_H */