#include "napi.h"

#include <assert.h>
#include <kernel/panic.h>
#include <kernel/tee_time.h>

#include <initcall.h>

#include <npf_router.h>

#include <drivers/bstgw/skbuff_stub.h>

#include <utils.h>

//#define BSTGW_NAPI_DEBUG

struct napi_dev_list napi_dlst;

// WARNING: must be initialised before vnic! + before notifier loop starts
TEE_Result napi_init(void) {
    napi_dlst.nq = (struct napi_queue)NAPI_QUEUE_INITIALIZER;
    napi_dlst.nq_slock = SPINLOCK_UNLOCK;
    return TEE_SUCCESS;
}

//driver_init(napi_init);
service_init_late(napi_init);

void napi_instance_setup(struct napi_struct *n, struct condvar *wait_cv, struct mutex *wait_mtx) {
    assert(n && wait_cv && wait_queue);
    memset(n, 0, sizeof(struct napi_struct));
    n->slock = SPINLOCK_UNLOCK;

    n->wait_cv = wait_cv;
    n->wait_mtx = wait_mtx;
}


void netif_napi_add(struct ifnet *dev, struct napi_struct *napi,
    int (*poll)(struct napi_struct *, int), int weight) {
    
    assert (dev != NULL && napi != NULL && poll != NULL);

    // fixme: add error return value
    if(dev->napi_count >= BSTGW_MAX_NETIF_NAPI) {
        EMSG("%s cannot support another NAPI struct", dev->name);
        panic("too many NAPI for device");
    }

    // fixme: currently only adding once, removing once works
    if(dev->napis[dev->napi_count]) {
        EMSG("%s has already a registered NAPI for index %u",
            dev->name, dev->napi_count);
        panic("corrupted NAPI array");
    }

    napi->net_dev = dev;
    napi->poll = poll;
    napi->weight = weight;

    napi->is_waiting = false;

#ifdef BSTGW_NAPI_DEBUG
    DMSG("Adding NAPI for %s with idx: %u", dev->name, dev->napi_count);
#endif

    napi->state = (1 << NAPI_STATE_DISABLE); // disabled by default; call napi-enable()
    napi->idx = dev->napi_count;
    dev->napis[napi->idx] = napi;
    dev->napi_count += 1;

    // Add to NAPI list
    cpu_spin_lock(&napi_dlst.nq_slock);
    SLIST_INSERT_HEAD(&napi_dlst.nq, napi, link);
    cpu_spin_unlock(&napi_dlst.nq_slock);
}

void netif_napi_del(struct napi_struct *napi) {
    assert(napi);
    
    // Remove from NAPI list
    cpu_spin_lock(&napi_dlst.nq_slock);
    SLIST_REMOVE(&napi_dlst.nq, napi, napi_struct, link);
    cpu_spin_unlock(&napi_dlst.nq_slock);

    // TODO: locking!
    napi->net_dev->napi_count -= 1;
    napi->net_dev->napis[napi->idx] = NULL; // dangerous
    napi->net_dev = NULL;
    napi->poll = NULL;
    napi->weight = 0;
    napi->state = (1 << NAPI_STATE_DISABLE);
}

void napi_disable(struct napi_struct *n) {
    if (n == NULL) panic("null on napi_disable");
    uint32_t exceptions;

    // set disable pending
    exceptions = cpu_spin_lock_xsave(&n->slock);
    if (n->state & (1 << NAPI_STATE_DISABLE)) panic("already disabled");
    n->state |= (1 << NAPI_STATE_DISABLE);

    while (true) {
        // if the worker is not (yet) active, we don't have to wait
        if (!(n->state & (1 << NAPI_STATE_W_ACTIVE))) {
            n->state &= ~(1 << NAPI_STATE_SCHED);
#if 0
            // but we want it to get rescheduled after napi_enable()
            n->state |= (1 << NAPI_STATE_MISSED);
#endif
            break;
        }

        // wait for worker to stop
        if (n->state & (1 << NAPI_STATE_SCHED)) {
            cpu_spin_unlock_xrestore(&n->slock, exceptions);
            //tee_time_wait(0); // WRN: system freeze when called from DA ctx
            __asm volatile("yield" ::: "memory");
            exceptions = cpu_spin_lock_xsave(&n->slock);
            continue;
        }
        break;
    }

    // sched NOT set anymore, i.e., not polling/will not poll
    if (!(n->state & (1 << NAPI_STATE_DISABLE))) panic("disabling failed");
    cpu_spin_unlock_xrestore(&n->slock, exceptions);
}

void napi_enable(struct napi_struct *n) {
    uint32_t exceptions;
    exceptions = cpu_spin_lock_xsave(&n->slock);

    if (!(n->state & (1 << NAPI_STATE_DISABLE))) panic("enabling not-disabled");
    if (n->state & (1 << NAPI_STATE_SCHED)) panic("enabling already sched'd");
    assert(!(n->state & (1 << NAPI_STATE_SCHED)));
    n->state &= ~(1 << NAPI_STATE_DISABLE);

#if 0
    if(n->state & (1 << NAPI_STATE_MISSED)) {
        n->state &= ~(1 << NAPI_STATE_MISSED);
        n->state |= (1 << NAPI_STATE_SCHED);
        cpu_spin_unlock_xrestore(&n->slock, exceptions);

        __napi_schedule(n); // kick
        IMSG("Triggered re-run after napi_enable.");
        return;
    }
#endif

    cpu_spin_unlock_xrestore(&n->slock, exceptions);
}

bool napi_schedule_prep_locked(struct napi_struct *n) {
    if(__predict_false(!n)) return false;

    if(__predict_false( n->state & (1 << NAPI_STATE_DISABLE) )) {
        return false;
    }

    // mark for later re-run but don't kick (already running)
    if (n->state & (1 << NAPI_STATE_SCHED)) {
        n->state |= (1 << NAPI_STATE_MISSED); // note: be careful, otherwise too many re-schedules?
        // TODO: this will happen super often atm, bcs. both queues share the NAPI state!
        return false; // don't kick
    }

    n->state |= (1 << NAPI_STATE_SCHED);
    n->state &= ~(1 << NAPI_STATE_MISSED);
    return true; // must be kicked (todo: spinlock guard prep and __schedule together)
}

void __napi_schedule_locked(struct napi_struct *n) {
    assert(n);
    n->do_notify = true;
}


/* Finished. If DISABLED, do not re-run. If not !(DISABLED) and MISSED flag was set, re-run */
bool napi_complete_done(struct napi_struct *n, int work_done __unused) {
    assert (napi != NULL);
    uint32_t exceptions = cpu_spin_lock_xsave(&n->slock);

    // disable has priority over missed atm (re-added)
    if( __predict_true(!(n->state & (1 << NAPI_STATE_DISABLE))) &&
        (n->state & (1 << NAPI_STATE_MISSED)) ) {

        n->state &= ~(1 << NAPI_STATE_MISSED);
        cpu_spin_unlock_xrestore(&n->slock, exceptions);
        return false; // no need to re-enable interrupts
    }

    n->state &= ~(1 << NAPI_STATE_SCHED);
    cpu_spin_unlock_xrestore(&n->slock, exceptions);
    return true;
}

// -> router
/* Assumes worker idx 0 == rx worker */
int bstgw_generic_rx_handler(struct ifnet *net_dev,
    bstgw_ethbuf_t **pkts /* *pkts[] */, unsigned int pktcount) {
    if(__predict_false( !net_dev->worker_count || !net_dev->workers[0] )) {
        EMSG("No rx router worker has been registered, yet");
        return -1;
    }

    router_if_input(net_dev->workers[0], pkts, pktcount);
    return 0;
}
