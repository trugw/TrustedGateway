#ifndef BSTGW_RINGBUFFER_H
#define BSTGW_RINGBUFFER_H

#include <stdlib.h>
#include <stdint.h>

// TODO: want bigger tx and small rx size, because we only receive small data
struct msg_buffer {
    // warning: currently it is assumed in the shm_rb that buffer is at index 0
    char buffer[2048];
    uint32_t head;
    uint32_t tail;
    uint32_t full; // 0: no, != 0: yes
    /* TODO: changed head,tail,full from `int` to uint32_t to avoid bit-size problems; CHECK USEAGE! */
};
typedef struct msg_buffer sw_buff_t;

int init_sw_buffer(sw_buff_t *);
int free_sw_buffer(sw_buff_t *);
int put_sw_buffer(sw_buff_t *, const void *, size_t);
int get_sw_buffer(sw_buff_t *, void *, size_t);

/* TODO: might not be needed / make sense later? */
uint32_t spaceleft_sw_buffer(const sw_buff_t *); // TODO: CHECK USAGES (int->uin32_t)
uint32_t dataleft_sw_buffer(const sw_buff_t *); // TODO: CHECK USAGES (int->uin32_t)

#endif /* BSTGW_RINGBUFFER_H */
