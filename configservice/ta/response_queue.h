#ifndef BSTGW_RESPONSE_QUEUE_H
#define BSTGW_RESPONSE_QUEUE_H

#include <stdlib.h>

struct resp_chunk {
    void *buf;
    size_t len;
    size_t offset;
    int take_ownership; // default: 0; | if != 0, will free() buf on chunk delete
    struct resp_chunk *next;
};

void free_resp_queue(struct resp_chunk *q);
void append_resp_queue(struct resp_chunk **qp, struct resp_chunk *entry);
void delete_head_resp_queue(struct resp_chunk **qp);
struct resp_chunk *pack_into_resp_chunk(const void *, size_t);

#endif /* BSTGW_RESPONSE_QUEUE_H */