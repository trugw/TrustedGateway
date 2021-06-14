#include "response_queue.h"
#include <stdlib.h>
#include <tee_internal_api.h> // *MSG()

void free_resp_queue(struct resp_chunk *q) {
    struct resp_chunk *curr, *next = NULL;
    for (curr = q; curr != NULL; curr = next) {
        next = q->next;
        if (q->buf != NULL && q->take_ownership) free(q->buf);
        free(q);
    }
}

void append_resp_queue(struct resp_chunk **qp, struct resp_chunk *entry) {
    if (qp == NULL) abort();
    if (entry == NULL) return;
    struct resp_chunk **tail;
    for (tail = qp; *tail != NULL; tail = &(*tail)->next);
    *tail = entry;
    entry->next = NULL; // just to be sure
}

void delete_head_resp_queue(struct resp_chunk **qp) {
    if (qp == NULL || *qp == NULL) return;
    struct resp_chunk *next = (*qp)->next;
    (*qp)->next = NULL;
    free_resp_queue(*qp);
    *qp = next;
}

struct resp_chunk *pack_into_resp_chunk(const void *buf, size_t len) {
    if (buf == NULL) return NULL;
    struct resp_chunk *data = malloc(sizeof(struct resp_chunk));
    if (data == NULL) { EMSG("Out of memory in pack chunk\n"); return NULL; }
    data->buf = (void *)buf;
    data->len = len;
    data->offset = 0;
    data->take_ownership = 0; // default: no
    data->next = NULL;
    return data;
}