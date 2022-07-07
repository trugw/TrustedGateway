#ifndef TZR_PTH_WRAP_H
#define TZR_PTH_WRAP_H

#include <kernel/panic.h>

// default mode
#define PTHREAD_MTX_MODE

#ifdef PTHREAD_MTX_MODE

#include <kernel/mutex.h>
typedef struct mutex pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER MUTEX_INITIALIZER

#define pthread_mutex_lock(m)		mutex_lock(m)
#define pthread_mutex_unlock(m) 	mutex_unlock(m)
#define pthread_mutex_destroy(m)	mutex_destroy(m)
#define pthread_mutex_init(m, x)	mutex_init(m)

#else

#include <kernel/spinlock.h>

typedef unsigned int pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER SPINLOCK_UNLOCK

// TODO: do OP-TEE's slocks always require interrupts to get disabled?
#define pthread_mutex_lock(m)		cpu_spin_lock(m)
#define pthread_mutex_unlock(m) 	cpu_spin_unlock(m)
#define pthread_mutex_destroy(m)
#define pthread_mutex_init(m, x)	*m = SPINLOCK_UNLOCK

#endif /* PTHREAD_MTX_MODE */

// not supported, bcs. OP-TEE's trusted threads only last for a single (P)TA call
typedef int pthread_key_t;
#define pthread_key_create(k,d)     panic("pthread_key_create not supported")
#define pthread_key_delete(k)       panic("pthread_key_delete not supported")
#define pthread_getspecific(k)      panic("pthread_getspecific not supported")
#define pthread_setspecific(k,v)    panic("pthread_setspecific not supported")

#endif /* TZR_PTH_WRAP_H */
