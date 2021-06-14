#ifndef TZR_TRUST_ROUTER_H
#define TZR_TRUST_ROUTER_H

#include "npf_router.h"
#include <bstgw/worker_storage.h>

typedef struct worker_sess_info {
	/* offset 0: (wrk_ls_t *) */
	union {
		// npf_workerinfo_t *npf_cleaner; // TODO: static type + singleton instance atm
		worker_t *router_worker;
		wrk_ls_t *npf_worker_data;
	} worker;
	int type;
} wrk_sess_t;

#endif /* TZR_TRUST_ROUTER_H */