#ifndef TZR_WORKER_STORAGE_H
#define TZR_WORKER_STORAGE_H

#include <ebr.h>

struct npf_statistics_tls;

struct worker_local_storage {
	ebr_tls_t *ebr_tls;
	struct npf_statistics_tls *npf_stats_tls;
};
typedef struct worker_local_storage wrk_ls_t;

ebr_tls_t *get_worker_epoch(void);
int set_worker_epoch(ebr_tls_t *);

struct npf_statistics_tls *get_worker_stats(void);
int set_worker_stats(struct npf_statistics_tls *);

#endif /* TZR_WORKER_STORAGE_H */