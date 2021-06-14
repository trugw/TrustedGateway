#include "shm_rb.h"

#include <assert.h>

TEEC_Result init_shm_rb(TEEC_Context *ctx, shm_rb_t *shrb) {
    if (shrb == NULL) return TEEC_ERROR_GENERIC;

    /* Setup shared memory */
    shrb->shm.size = sizeof(sw_buff_t);
    shrb->shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
    TEEC_Result ret = TEEC_AllocateSharedMemory(ctx, &shrb->shm);
    if(ret != TEEC_SUCCESS) return ret;
    
    /* Init ring buffer in shm */
    assert(shrb->shm.buffer != NULL && shrb->shm.size >= sizeof(sw_buff_t));
    shrb->rb = shrb->shm.buffer;
    if(init_sw_buffer(shrb->rb) != 0) {
        TEEC_ReleaseSharedMemory(&shrb->shm);
        return TEEC_ERROR_GENERIC;
    }

    return TEEC_SUCCESS;
}
TEEC_Result free_shm_rb(shm_rb_t *shrb) {
    if(shrb == NULL) return -1;
    free_sw_buffer(shrb->rb);
    TEEC_ReleaseSharedMemory(&shrb->shm);
    return 0;
}

int put_shm_rb(shm_rb_t *shrb, const void *buf, size_t len) {
    if(shrb == NULL) return -1;
    return put_sw_buffer(shrb->rb, buf, len);
}
int get_shm_rb(shm_rb_t *shrb, void *buf, size_t len) {
    if(shrb == NULL) return -1;
    return get_sw_buffer(shrb->rb, buf, len);
}
int spaceleft_shm_rb(const shm_rb_t *shrb) {
    if(shrb == NULL) return -1;
    return spaceleft_sw_buffer(shrb->rb);
}
int dataleft_shm_rb(const shm_rb_t *shrb) {
    if(shrb == NULL) return -1;
    return dataleft_sw_buffer(shrb->rb);
}