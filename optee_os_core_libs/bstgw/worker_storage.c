#include <bstgw/worker_storage.h>

#include <net/npf.h>

#include <kernel/tee_ta_manager.h>
#include <kernel/panic.h>
#include <assert.h>

static wrk_ls_t *get_worker_local_data(void) {
    struct tee_ta_session *sess;
    if((tee_ta_get_current_session(&sess) != TEE_SUCCESS)
        || sess->user_ctx == NULL) return NULL;

    /* must be in sync. with session info;
     * better: start session info with a type/magic value */
    wrk_ls_t **s = sess->user_ctx;
    if (*s == NULL) panic("No worker, so prob. got called on incompatible session");
    return *s;
}

ebr_tls_t *get_worker_epoch(void) {
    wrk_ls_t *wls = get_worker_local_data();
    if (wls == NULL) panic("get_worker_epoch() call with NULL worker storage");
    return wls->ebr_tls;
}

int set_worker_epoch(ebr_tls_t *t) {
    wrk_ls_t *wls = get_worker_local_data();
    if (wls == NULL) panic("set_worker_epoch() call with NULL worker storage");
    /* NOTE: we don't free here, bcs. ebr uses the API directly and it calls
     * free(wls->ebr_tls); already externally
     */
    wls->ebr_tls = t;
    return 0;
}

npf_stats_tls *get_worker_stats(void) {
    wrk_ls_t *wls = get_worker_local_data();
    assert(wls != NULL);
    return wls->npf_stats_tls;
}

int set_worker_stats(npf_stats_tls *st) {
    wrk_ls_t *wls = get_worker_local_data();
    assert(wls != NULL);

    /* already free'd externally, so don't call free() */

    wls->npf_stats_tls = st;
    return 0;
}