/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

#include "utils.h"
#include "npf_router.h"

#include <npf_firewall/npfkern.h>

#include "trust_router.h"

#include <tee/entry_std.h>

#include <napi.h>

#include <initcall.h>

//#define BSTGW_NPF_ROUTER_DBG

#define	BURST_SIZE		(512) //(256)
#define	NUM_MBUFS		((8 * 1024) - 1)
#define	MBUF_CACHE_SIZE		(256)

#ifndef unlikely
#define unlikely(x)      __builtin_expect(!!(x), 0) 
#endif


/* ********************************************************************** */
/* TODO(!):  race conditions between shutdown and NW threads calling in ! */
/* ********************************************************************** */

uint32_t router_in_burst_size(ifnet_t *ndev, ifnet_t *dst_if) {
	assert(ndev && trusted_router);
	uint32_t exceptions, burst;
	ifnet_t *fec_ndev;
	pktqueue_t *pq;

	// optional (can avoid slock collision at ifnet_getbyname)
	if (dst_if) {
		pq = dst_if->egress_queue;
		exceptions = cpu_spin_lock_xsave(&pq->slock);
		burst = pq->capacity - pq->count;
		cpu_spin_unlock_xrestore(&pq->slock, exceptions);
		return burst;
	}

	switch(ndev->trust_status) {
		case BSTGW_IF_NONSEC:
			fec_ndev = ifnet_getbyname("imx6q-fec");
			if(unlikely( !fec_ndev )) panic("Failed to get fec ndev");
			pq = fec_ndev->egress_queue;

			exceptions = cpu_spin_lock_xsave(&pq->slock);
			burst = pq->capacity - pq->count;
			cpu_spin_unlock_xrestore(&pq->slock, exceptions);
			return burst;

		case BSTGW_IF_EXTERNAL:
			// todo
			return trusted_router->pktqueue_size / 2;
		
		default:
			panic("Unknown trust status");
	}
}

static int worker_init(worker_t **);
static int worker_fini(worker_t *);

static void	router_destroy(npf_router_t *);

int worker_get_number(void) {
	if (trusted_router == NULL) return -1;
	return trusted_router->worker_count; // todo: or missing ones?
}

static int worker_init(worker_t **wp)
{
	IMSG("Init a firewall worker");
	npf_router_t *router = trusted_router;
	if (router == NULL || wp == NULL || *wp != NULL) {
		EMSG("router was NULL, w == NULL or *w != NULL");
		return -1;
	}

	/* Initialize the worker structure. */
	IMSG("Will call calloc() now");
	*wp = calloc(1, sizeof(worker_t));
	if (*wp == NULL) {
		EMSG("worker calloc failed");
		return -1;
	}
	struct worker *w = *wp;
	IMSG("Assigning router and router->npf now");
	w->router = router;

	/* Register the NPF worker. */
//	npfk_thread_register(router->npf);
	w->npf = router->npf;

	IMSG("Worker init done");
	return 0;
}

static int worker_fini(worker_t *worker)
{
	if (!worker) {
		return 0;
	}

	npf_router_t *router = trusted_router;
	if (router == NULL) return -1;

	// TODO: unbind session + free potential epoch data || race with existing threads
	if (worker->bind.status == WORKER_IS_BOUND) {
		struct tee_ta_session_head *open_sessions;
		nsec_sessions_list_head(&open_sessions);
		if (open_sessions == NULL) panic("Bound worker w/o open session");

		cpu_spin_lock(&worker->bind.slock);

		struct tee_ta_session *s = tee_ta_get_session(worker->bind.sess_id, true, // or false?
			open_sessions);
		if (s == NULL || s->user_ctx == NULL) EMSG("WARNING: worker's session ID was invalid!");
		else {
			// nullify pointer to worker (TODO: set type CLEANED or whatever)
			((wrk_sess_t *)s->user_ctx)->worker.router_worker = NULL; // TODO: check for NULL @ trust_router.c or risk NULLPTR crash
		}

		worker->bind.status = WORKER_UNBOUND;
		worker->bind.sess_id = 0;
		// free memory
		if (worker->worker_npf_data.ebr_tls != NULL) {
			free(worker->worker_npf_data.ebr_tls);
			worker->worker_npf_data.ebr_tls = NULL;
		}

		cpu_spin_unlock(&worker->bind.slock);
	}

//	npfk_thread_unregister(router->npf);

	free(worker);
	return 0;
}

worker_t *bind_to_worker(void) {
	if (trusted_router == NULL) return NULL;

	struct tee_ta_session *s;
	tee_ta_get_current_session(&s);
	if (s == NULL || s->user_ctx == NULL) {
		EMSG("Cannot bind sesson-less thread to router worker");
		return NULL;
	}

	for (unsigned int i=0; i < trusted_router->worker_count; i++) {
		worker_t *w = trusted_router->worker[i];
		if (w == NULL) continue;

		cpu_spin_lock(&w->bind.slock);
		if (w->bind.status == WORKER_UNBOUND) {
			w->bind.status = WORKER_IS_BOUND;
			w->bind.sess_id = s->id;

			// TODO: new, bcs. otherwise thread_register() will try to access worker
			// 		 storage while it is still NULL
			((wrk_sess_t *)s->user_ctx)->worker.router_worker = w;

			// TODO: WARNING(!) -- calling this multiple times sounds wrong with new storage?!
			// -- wasn't the idea to let the TLS data survive even if NW thread changes?!
			// TODO: or keep, bcs. why should non-active ones be EBR-notified?!
			// BUT(!): A NO GO REGARDING THE NPF STATISTICS, bcs. they would be nulled all the time!
			// BUT(!): thread_unregister() != statistics cleanup; only cleanup of EBR struct
			npfk_thread_register(trusted_router->npf);
			cpu_spin_unlock(&w->bind.slock);
			return w;
		}
		cpu_spin_unlock(&w->bind.slock);
	}
	return NULL;
}

// TODO: add error return value?
void detach_nw_from_worker(worker_t *w) {
	if (w == NULL) return;
	if(w->router == NULL) panic("Detaching from worker with unspecified router object");
	if(w->router->npf == NULL) panic("Detaching from worker with incompletely initialized router object (NPF missing)");

	// TODO: required?
/*
	struct tee_ta_session *s;
	tee_ta_get_current_session(&s);
	if (s == NULL) {
		EMSG("No session to detach");
		return;
	}
*/

	cpu_spin_lock(&w->bind.slock);
//	if (s->id == w->bind.sess_id) {
		w->bind.status = WORKER_UNBOUND;
		w->bind.sess_id = 0;
		// TODO: WARNING(!) -- calling this each time sounds wrong with new storage?!
		// -- wasn't the idea to let the TLS data survive even if NW thread changes?!
		npfk_thread_unregister(w->router->npf); // w->router
//	} else {
//		EMSG("Mismatching session ID, cannot detach!");
//	}
	cpu_spin_unlock(&w->bind.slock);
}


// TODO: how to stop from external? (a flag settable via separate NW->SW call?)
int worker_run(worker_t *worker) {
	assert(worker && trusted_router);
	if (!worker->net_if) {
		EMSG("The worker has no attached networker interface/device");
		return -1;
	}
	struct napi_struct *napi = worker->net_if->napis[worker->napi_idx];
	assert(napi && napi->poll);
	uint32_t exceptions;

	// the weight/budget values are not interesting atm
	int budget = 100000, pkts = 0;
	volatile unsigned int ncycles = 31250;
	int nrounds = 640;

	// router + xmit of FEC
	if(worker->napi_idx == BSTGW_WRK_OUT) {
		/* FEC xmit napi */
		ncycles = 5000;
		nrounds = 4000;
	}

	// TODO:  set_link_up(napi->net_dev)

#ifdef BSTGW_NPF_ROUTER_DBG
	DMSG("Starting worker loop");
#endif

	/* TODO: where to put this?! */
	exceptions = cpu_spin_lock_xsave( &napi->slock );
	napi->state |= (1 << NAPI_STATE_W_ACTIVE);
	cpu_spin_unlock_xrestore( &napi->slock, exceptions );

	bool run_another_round;

	if(!mutex_trylock( napi->wait_mtx )) goto re_cycle;
	// TODO: add stop flag, or number of round (but in that case don't set the link down)
	while (true) {
		/* Safely grab condition state */
		exceptions = cpu_spin_lock_xsave( &napi->slock );
		if ( napi->state & (1 << NAPI_STATE_SCHED) ) {
			run_another_round = true;
			napi->do_notify = false; // disable a potentially pending kick
		}
		else run_another_round = false;
		cpu_spin_unlock_xrestore( &napi->slock, exceptions );


		/* If not fufilled: sleep, then retry on wakeup */
		if (!run_another_round) {
//#ifdef BSTGW_NPF_ROUTER_DBG
			DMSG("Going to sleep for activity of device: %s (idx: %u)", napi->net_dev->name, worker->napi_idx);
//#endif
			napi->is_waiting = true; // guard: wait_mtx
			condvar_wait( napi->wait_cv, napi->wait_mtx );
			continue; // re-check condition (we hold the wait_mtx)
		} else {
			napi->is_waiting = false; // guard: wait_mtx
		}


		/* Else: time to poll the device */
		mutex_unlock( napi->wait_mtx );

		for(;;) {
re_poll:
			pkts = napi->poll( napi, budget );
#ifdef BSTGW_NPF_ROUTER_DBG
			DMSG("napi->poll() >> %d", pkts);
#endif

			//todo: remove completely
			if(pkts >= budget) {
#ifdef BSTGW_NPF_ROUTER_DBG
				DMSG("pkts >= budget");
#endif
				continue;
			}

			//todo
			if(unlikely(pkts < 0)) {
				EMSG("error in poll() method of %s: %d", napi->net_dev->name, pkts);
				goto worker_out;
			}

re_cycle:
			// try to avoid the costly path (TODO: implement low-latency sched.)
			for(unsigned int i=0; i<nrounds; i++) {
				// todo: slock contention!
				cpu_spin_lock( &napi->slock );
				if( napi->state & (1 << NAPI_STATE_SCHED) ) {
					napi->do_notify = false;
					cpu_spin_unlock(&napi->slock);
					goto re_poll;
				}
				cpu_spin_unlock(&napi->slock);
				bstgw_spin_cycles(&ncycles);
			}

			// prevent extra sleeps on mutex-lock by re-trying on a mutex coll.
			if(!mutex_trylock(napi->wait_mtx)) goto re_cycle;

			break;


		}
	}

worker_out:
	/* TODO: where to put this?! */
	exceptions = cpu_spin_lock_xsave( &napi->slock );
	napi->state &= ~(1 << NAPI_STATE_W_ACTIVE);
	cpu_spin_unlock_xrestore( &napi->slock, exceptions );

	// TODO:  set_link_down(napi->net_dev)

	return 0;
}


wrk_ls_t *bind_to_npfworker(void) {
	// TODO: race condition w.r.t. regarding router cleanup?!
	if (trusted_router == NULL) return NULL;
	return npfk_bind_npfworker();
}


static int gc_notifier_fct(void *data __unused) {
	if (! npfk_is_bound_worker()) return -1;
	return (int)npfk_worker_iteration();
}

void npfworker_run(void) {
	assert(trusted_router);
	bstgw_notifier_loop(&trusted_router->notfer, true);
}


void detach_nw_from_npfworker(void) {
	// TODO: race condition w.r.t. regarding router cleanup?!
	if (trusted_router == NULL) return;
	npfk_unbind_npfworker();
}

/* Mostly Secure World, maybe parts split out? */
static npf_router_t *
router_create(unsigned int nworkers)
{
	IMSG("Create router and firewall data structures");
	npf_router_t *router;
	size_t len;

	// note: we need n workers, plus 1 notifier/cleaner, plus at least 1 for the rest
	if ( (CFG_NUM_THREADS - 2) < nworkers ) {
		EMSG("Not enough threads for the TrustedGateway");
		return NULL;
	}
	if ( (CFG_TEE_CORE_NB_CORE - 2) < nworkers ) {
		IMSG("Performance might degrade, because not enough cores to run the important tasks on separate cores");
	}

	/*
	 * Allocate the router structure.
	 */
	len = offsetof(npf_router_t, worker[nworkers]);
	if ((router = calloc(1, len)) == NULL) {
		return NULL;
	}
	router->worker_count = nworkers;
	router->pktqueue_size = BURST_SIZE;

	/*
	 * Initialize mbuf pool.
	 */
/*
	router->mbuf_pool = rte_pktmbuf_pool_create("mbuf-pl",
	    number:  NUM_MBUFS * MAX_IFNET_IDS [~8k * 32],
		cache size:  MBUF_CACHE_SIZE [256],
	    priv size:   RTE_CACHE_LINE_ROUNDUP(sizeof(npf_mbuf_priv_t)),
	    data room size:   RTE_MBUF_DEFAULT_BUF_SIZE (2048 + 128 [headroom]),
		SOCKET_ID_ANY);
	if (!router->mbuf_pool) {
		free(router);
		return NULL;
	}
 */

	/*
	 * NPF instance and its operations.
	 */

	// WARNING: might have to somehow prepare some data structures/pointers through to catch signalling
	// although npf cleaner is uninitialized.
	npfk_sysinit();

	router->npf = npf_dpdk_create(0, router);
	if (!router->npf) {
		goto err;
	}
	if (npf_alg_icmp_init(router->npf) != 0) {
		goto err;
	}

	router->rtable = route_table_create();
	if (!router->rtable) {
		goto err;
	}

	return router;
err:
	router_destroy(router);
	return NULL;
}

/* all/mostly Secure World */
static void
router_destroy(npf_router_t *router)
{
	// Free initialized workers and detach associated ifnets
	for( unsigned i=0; i < router->worker_count; i++) {
		worker_t *w = router->worker[i];
		if (!w) continue;

		ifnet_t *ifp = w->net_if;
		if (ifp) {
			ifnet_ifdetach(router, ifp);
			w->net_if = NULL;
		}

		worker_fini(w);
		router->worker[i] = NULL;
	}

	if (router->rtable) {
		route_table_destroy(router->rtable);
	}

	if (router->npf) {
		npf_alg_icmp_fini(router->npf);
		npfk_destroy(router->npf);
	}
	npfk_sysfini();
	free(router);
}

npf_router_t *trusted_router = NULL;

static TEE_Result init_trusted_router(void)
{
	IMSG("Initialize trusted router and firewall...");
	if (trusted_router != NULL) {
		EMSG("Failure: already initialized");
		return TEE_ERROR_GENERIC;
	}

	// Note: won't change bcs. were're at init
	unsigned int nnapi;
	if( !ifnet_dev_count() || !(nnapi = ifnet_napi_count()) ) {
		EMSG("no routable interfaces");
		return TEE_ERROR_GENERIC;
	}

	// Setup the NPF router configuration. (with 1 worker per interface)
	if ((trusted_router = router_create(nnapi)) == NULL) return TEE_ERROR_GENERIC;

	// Load IP assignment and IP routing configurations
	IMSG("Load router configuration");
	if (load_config(trusted_router) == -1) {
		EMSG("Failed to load router configuration");
		router_destroy(trusted_router);
		return TEE_ERROR_GENERIC;
	}

	/*
	 * Initialize network interfaces and initialize one worker per interface.
	 */
	IMSG("Init firewall interfaces and workers");
	cpu_spin_lock(&netdev_list.slock);
	ifnet_t *net_dev; unsigned int i = 0;
	LIST_FOREACH(net_dev , &netdev_list.list, entry) {
		if ((i + (net_dev->napi_count-1)) >= trusted_router->worker_count) {
			EMSG("Unexpected:  more NAPI structs than workers");
			break;
		}

		if (net_dev->napi_count > BSTGW_MAX_NETIF_WORKER) {
			EMSG("Unexpected:  more NAPI structs than max. per-ndev workers");
			break;
		}

		/* Init 1 worker per NAPI struct */
		for (unsigned int j=0; j<net_dev->napi_count; j++) {
			if (worker_init(&trusted_router->worker[i]) < 0) {
				EMSG("Failed router worker initialization");
				router_destroy(trusted_router);
				return TEE_ERROR_GENERIC;
			}
			trusted_router->worker[i]->worker_id = i;
			//net_dev->rx_handler_data = trusted_router->worker[i];
			net_dev->workers[j] = trusted_router->worker[i];
			net_dev->worker_count += 1;
			trusted_router->worker[i]->net_if = net_dev;
			trusted_router->worker[i]->napi_idx = j;
			i++;
		}

		// currently a NOP
		if (ifnet_setup(trusted_router, net_dev) == -1) {
			EMSG("ifnet_setup failed");
			router_destroy(trusted_router);
			return TEE_ERROR_GENERIC;
		}
		if (ifnet_ifattach(trusted_router, net_dev) == -1) {
			EMSG("ifnet_ifattach failed");
			router_destroy(trusted_router);
			return TEE_ERROR_OUT_OF_MEMORY;
		}

#ifdef BSTGW_NON_NPF_ANTI_IP_SPOOFING
		if (net_dev->trust_status == BSTGW_IF_EXTERNAL) {
			if (trusted_router->ext_ndevs_count >= BSTGW_MAX_EXT_NDEVS) {
				EMSG("Warning: too many external interface, will ignore next one for anti IP spoofing; please increase BSTGW_MAX_EXT_NDEVS!");
			} else {
				trusted_router->ext_ndevs[trusted_router->ext_ndevs_count++] = net_dev;
			}
		}
#endif

		IMSG("  configured network interface/dev %s (MAC: %x:%x:%x:%x:%x:%x)",
			net_dev->name, net_dev->hwaddr.addr_bytes[0], net_dev->hwaddr.addr_bytes[1],
			net_dev->hwaddr.addr_bytes[2], net_dev->hwaddr.addr_bytes[3],
			net_dev->hwaddr.addr_bytes[4], net_dev->hwaddr.addr_bytes[5]);
		IMSG("number of NAPIs: %u, workers: %u",
			net_dev->napi_count, net_dev->worker_count);
		for(unsigned int j=0; j<net_dev->ips_cnt; j++)
			IMSG("IP [%u]: 0x%x", j, net_dev->if_ips[j].word32[0]);

	}
	cpu_spin_unlock(&netdev_list.slock);

	// populate ethernet buffer pool with space for ARP, NPF buffers, and
	// FEC bursts (cf. router_in_burst_size)
// TODO: increase heap, then increase buffer number (cf. vnet)
//	bstgw_ethpool_increase(NULL, 512 + 512 + 256 );
	bstgw_ethpool_increase(NULL, 2 + 2 + 64 );

    bstgw_init_notifier(&trusted_router->notfer, 1 /* check period [ms] */,
		gc_notifier_fct /*idle fct*/, NULL /*worker data*/);

	IMSG("... Success.");
	return TEE_SUCCESS;
}

driver_init_late(init_trusted_router);
