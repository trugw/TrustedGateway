#ifndef TZR_WORKER_BINDING_H
#define TZR_WORKER_BINDING_H

#include <stdint.h>

typedef enum {
	WORKER_UNBOUND,
	WORKER_IS_BOUND,
} bind_stat_t;

typedef struct {
	unsigned int    slock;
	bind_stat_t 	status;
	uint32_t 		sess_id;       /* Session handle (0 is invalid) */
} worker_bind_t;

#endif /* TZR_WORKER_BINDING_H */