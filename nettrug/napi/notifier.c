#include "notifier.h"

#include <kernel/tee_time.h>
#include <sys/queue.h>

#include <assert.h>

#include <bstgw/bstgw_counter.h>

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0)
#endif

//#define BSTGW_NOTF_DEBUG

void bstgw_init_notifier(struct notifier *n, int check_period_ms,
    int (*idle_function)(void *), void *worker_data) {

    if (n == NULL) return;
    n->nl = &napi_dlst;
    n->idle_func = idle_function;
    n->worker_data = worker_data;
    n->check_period_ms = check_period_ms;
    atomic_flag_test_and_set(&n->cnt_flag);
}

static bool bstgw_notifier_wakeup_check(struct notifier *n) {
    struct napi_struct *napi_dev;
    uint32_t exceptions;

    // Wake up NAPI-enabled devices (TODO: unlock list slock before taking mutex?)
    cpu_spin_lock(&n->nl->nq_slock);
    bool kicked = false;
    SLIST_FOREACH(napi_dev, &n->nl->nq, link) {
        /* don't force a running worker to wake us due to lock contention!
            * instead:  retry next round */
        if(!mutex_trylock(napi_dev->wait_mtx)) continue;
        //mutex_lock(napi_dev->wait_mtx);

        // fast path: not waiting, no need to check (reduce slock contention)
        if(!napi_dev->is_waiting) {
            mutex_unlock(napi_dev->wait_mtx);
            continue;
        }

        /* check if kick is requested */
        //cpu_spin_lock(&napi_dev->slock);
        exceptions = cpu_spin_lock_xsave(&napi_dev->slock);

        bool signal_poll = napi_dev->do_notify;
        napi_dev->do_notify = false;

        //cpu_spin_unlock(&napi_dev->slock);
        cpu_spin_unlock_xrestore(&napi_dev->slock, exceptions);

        /* kick */
        if (signal_poll) {
#ifdef BSTGW_NOTF_DEBUG
            DMSG("going to signal a napi device");
#endif
            //TODO: not yet clear what makes more sense if >1 worker shared mtx
            condvar_signal(napi_dev->wait_cv);
            //condvar_broadcast(napi_dev->wait_cv);
            kicked = true;
        }

        mutex_unlock(napi_dev->wait_mtx);
    }
    cpu_spin_unlock(&n->nl->nq_slock);

    return kicked;
}

/*
 * Tries to check every n->check_period ms if NAPIs should be kicked.
 * 
 * n->idle_func() will start getting called after the first kick
 * with waiting periods in between based on its return value.
 * 
 * TODO: there is room for improvement here
 */
void bstgw_notifier_loop(struct notifier *n, bool delay_idle_till_first_kick) {
    assert(n);

    bool kicked, run_idle = !delay_idle_till_first_kick;
    int idle_func_sleep = 0;

    // check stop flag
    while(likely( atomic_flag_test_and_set(&n->cnt_flag) )) {
#ifdef BSTGW_NOTF_DEBUG
        DMSG("notifier poll loop iteration");
#endif

        kicked = bstgw_notifier_wakeup_check(n);
        if(unlikely( !run_idle ) && kicked) run_idle = true;

        // Run idle function if defined and optional extra wait time has passed
        if(likely( n->idle_func && run_idle )) {
            idle_func_sleep -= (n->check_period_ms + 10);
            if(unlikely( idle_func_sleep <= 0 )) {
#ifdef BSTGW_NOTF_DEBUG
                DMSG("going to call NPF cleaner");
#endif
                if (unlikely( (idle_func_sleep = n->idle_func(n->worker_data)) < 0 )) {
                    EMSG("An error has occured in the notified function");
                    atomic_flag_clear(&n->cnt_flag);
                }

                // rerun wakeup check
//                bstgw_notifier_wakeup_check(n);

            }
            // TODO: should kick more freq. when high connection load?
            //       (but doesn't the NPF-internal sched. tuning handle that?)
        }

        // wait check period
#ifdef BSTGW_NOTF_DEBUG
        DMSG("going to sleep for %d milliseconds", n->check_period_ms);
#endif
        tee_time_wait(n->check_period_ms);
#if (!defined(CFG_SECURE_TIME_SOURCE_CNTPCT)) && defined(CFG_BSTGW_CNTR)
        bstgw_incr_milli_counter(n->check_period_ms);
#endif
    }
}

// TODO
void bstgw_fini_notifier(struct notifier *n __unused) {
    EMSG("bstgw_fini_notifier() not implemented");
}