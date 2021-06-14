#include <bstgw/ringbuffer.h>

#include <string.h>
#include <stdlib.h> // abort

#ifdef BSTGW_TA
//#define DEBUG_PRINT
#endif

#ifdef DEBUG_PRINT
#include <tee_internal_api.h>
#endif

int init_sw_buffer(sw_buff_t *buff) {
    if (buff == NULL) return -1;
    buff->head = 0;
    buff->tail = 0;
    buff->full = 0;
    return 0;
}

int free_sw_buffer(sw_buff_t *buff) {
    if (buff == NULL) return -1;
    return 0;
}

uint32_t spaceleft_sw_buffer(const sw_buff_t *buff) {
    if (buff == NULL) return -1;
    if (buff->head == buff-> tail) {
        if (buff->full) return 0;
        else return sizeof(buff->buffer);
    }
    if (buff->tail < buff->head) return buff->head - buff->tail;
    else return sizeof(buff->buffer) - buff->tail + buff->head;
}

uint32_t dataleft_sw_buffer(const sw_buff_t *buff) {
    if (buff == NULL) return -1;
    if (buff->head == buff->tail) {
        if (buff->full) return sizeof(buff->buffer);
        else return 0;
    }
    if (buff->head < buff->tail) return buff->tail - buff->head;
    else return sizeof(buff->buffer) - buff->head + buff->tail;
}

/* !! TODO: Cache all values to prevent parallel modification (if real-time shared memory) !! */
int put_sw_buffer(sw_buff_t *sw_buff, const void *buf, size_t len) {
    if (sw_buff->full) abort(); // TODO
    const void *src = buf;    
    void *dst = sw_buff->buffer + sw_buff->tail;

    uint32_t spaceleft = spaceleft_sw_buffer(sw_buff);
    if (spaceleft > sizeof(sw_buff->buffer)) abort();
    size_t sent = len;
    if ( len > spaceleft ) {
        sent = spaceleft;
        sw_buff->full = 1; // will be full
    }
#ifdef DEBUG_PRINT
    DMSG("START[%p] head: %d, tail: %d, put: %ld, full: %d\n", sw_buff, sw_buff->head, sw_buff->tail, sent, sw_buff->full);
#endif

    // TODO: probably could just use the 2nd condition here
    if ( (sw_buff->tail < sw_buff->head)
        || (sent <= (sizeof(sw_buff->buffer) - sw_buff->tail))) {
        
        /* !! TODO: Check that no out-of-buffer read will happen, bcs. of malicious index values !! */
        memcpy(dst, src, sent);
    }
    // harder case:  cycle
    else {
        size_t tillend =  sizeof(sw_buff->buffer) - sw_buff->tail; //spaceleft - sw_buff->head;
        
        /* !! TODO: Check that no out-of-buffer read will happen, bcs. of malicious index values !! */
        memcpy(dst, src, tillend);
        memcpy(sw_buff->buffer, (uint8_t*)src + tillend, sent-tillend); // spaceleft-tillend
    }

    sw_buff->tail = (sw_buff->tail + sent) % sizeof(sw_buff->buffer);
#ifdef DEBUG_PRINT
    DMSG("END[%p] head: %d, tail: %d, put: %ld, full: %d\n", sw_buff, sw_buff->head, sw_buff->tail, sent, sw_buff->full);
#endif
    return sent;
}

/* !! TODO: Cache all values to prevent parallel modification (if real-time shared memory) !! */
int get_sw_buffer(sw_buff_t *sw_buff, void *buf, size_t len) {
    if (sw_buff->head == sw_buff->tail && !sw_buff->full) abort();
    const void *src = sw_buff->buffer + sw_buff->head;
    void *dst = buf;    

    uint32_t dataleft = dataleft_sw_buffer(sw_buff);
    if (dataleft > sizeof(sw_buff->buffer)) abort();
    size_t nread = len;
    if ( len > dataleft ) {
        nread = dataleft;
    }
    if (nread > 0) { sw_buff->full = 0; } // will not be full in any case
#ifdef DEBUG_PRINT
    DMSG("START[%p] head: %d, tail: %d, get: %ld, full: %d\n", sw_buff, sw_buff->head, sw_buff->tail, nread, sw_buff->full);
#endif

    // TODO: probably could just use the 2nd condition here
    if ( (sw_buff->head < sw_buff->tail)
        || (nread <= (sizeof(sw_buff->buffer) - sw_buff->head))) {

        /* !! TODO: Check that no out-of-buffer read will happen, bcs. of malicious index values !! */
        memcpy(dst, src, nread);
    }
    // harder case:  cycle
    else {
        size_t tillend = sizeof(sw_buff->buffer) - sw_buff->head; // dataleft - sw_buff->tail;

        /* !! TODO: Check that no out-of-buffer read will happen, bcs. of malicious index values !! */
        memcpy(dst, src, tillend);
        memcpy((uint8_t*)dst + tillend, sw_buff->buffer, nread-tillend); // dataleft-tillend
    }

    sw_buff->head = (sw_buff->head + nread) % sizeof(sw_buff->buffer);
#ifdef DEBUG_PRINT
    DMSG("END[%p] head: %d, tail: %d, get: %ld, full: %d\n", sw_buff, sw_buff->head, sw_buff->tail, nread, sw_buff->full);
#endif
    return nread;
}
