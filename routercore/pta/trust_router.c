#include <kernel/pseudo_ta.h>
#include <kernel/user_ta.h>

#include <tee_api_defines_extensions.h>
#include <tee_api_defines.h>

#include <bstgw/pta_trustrouter.h>
#include <bstgw/worker_storage.h>

#include "trust_router.h"
#include "npf_router.h"
#include "utils.h"

#include "bstgw/web_stub_ta.h"

#include <string.h>

#define PTA_NAME "bstgw_trust_router.pta"


static TEE_Result bstgw_firewall_config(uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]);

static TEE_Result cmd_get_nworkers(wrk_sess_t *s, uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]);

static TEE_Result bstgw_get_some_stats(uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]);

static TEE_Result cmd_run_cleaner(wrk_sess_t *s);
static TEE_Result cmd_run_worker(wrk_sess_t *rtr_sess);


static TEE_Result open_session(uint32_t param_types,
			       TEE_Param params[TEE_NUM_PARAMS],
			       void **sess_ctx)
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_INPUT,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);

	DMSG("has been called");

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Get Requested Session Type */
	int sess_type = params[0].value.a;
	switch (sess_type) {
		case BSTGW_TRUST_ROUTER_UNBOUND_SESSION:
		case BSTGW_TRUST_ROUTER_CLEANER_SESSION:
		case BSTGW_TRUST_ROUTER_WORKER_SESSION:
			break;
		default:
			return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Allocate context */
	if( (*sess_ctx = calloc(1, sizeof(struct worker_sess_info))) == NULL) {
		EMSG("OOM");
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	((struct worker_sess_info *)(*sess_ctx))->type = sess_type;

	struct worker_sess_info *wrk_ctx = *sess_ctx;

	if (sess_type == BSTGW_TRUST_ROUTER_WORKER_SESSION) {
		//wrk_ctx->worker.router_worker = bind_to_worker();
		(void)bind_to_worker(); // TODO
		if (wrk_ctx->worker.router_worker == NULL) {
			EMSG("No unbound router thread available");
			free(sess_ctx);
			return TEE_ERROR_GENERIC;
		}
	} else if (sess_type == BSTGW_TRUST_ROUTER_CLEANER_SESSION) {
		wrk_ctx->worker.npf_worker_data = bind_to_npfworker();
		if (wrk_ctx->worker.npf_worker_data == NULL) {
			EMSG("Failed to bind to NPF worker (already bound? not initialized?)");
			free(sess_ctx);
			return TEE_ERROR_GENERIC;
		}
	}

	// TODO: CLEANER_SESSION

	return TEE_SUCCESS;
}

static void close_session(void *sess_ctx)
{
	if (sess_ctx == NULL) EMSG("Warning: close session did not cause a memory free");
	else {
		struct worker_sess_info *wrk_ctx = sess_ctx;
		switch(wrk_ctx->type) {
			case BSTGW_TRUST_ROUTER_UNBOUND_SESSION:
				break;
			case BSTGW_TRUST_ROUTER_CLEANER_SESSION: {
				if (wrk_ctx->worker.npf_worker_data == NULL) {
					EMSG("Unexpected empty npf_worker_data on close_session");
				} else {
					detach_nw_from_npfworker();
				}
				break;
			}
			case BSTGW_TRUST_ROUTER_WORKER_SESSION: {
				if (wrk_ctx->worker.router_worker == NULL) {
					EMSG("Unexpected empty router_worker on close_session");
				} else {
					detach_nw_from_worker(wrk_ctx->worker.router_worker);
				}
				break;
			}
			default:
				EMSG("Unkown session type on session close");
		}
		free(sess_ctx);
	}
	IMSG("Goodbye!\n");
}

static TEE_Result invoke_command(void *sess_ctx, uint32_t cmd_id,
				 uint32_t param_types,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	if (sess_ctx == NULL) {
		EMSG("Why is *sess_ctx NULL?!");
		return TEE_ERROR_GENERIC;
	}
	wrk_sess_t *sess = sess_ctx;

	uint32_t no_params_type = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

	switch (cmd_id) {
        case BSTGW_PTA_TRUST_ROUTER_CMD_CONFIG: {
			if (sess->type != BSTGW_TRUST_ROUTER_UNBOUND_SESSION) return TEE_ERROR_ACCESS_DENIED;
            return bstgw_firewall_config(param_types, params);
        }

		case BSTGW_PTA_TRUST_ROUTER_CMD_GET_NWORKERS: {
			if (sess->type != BSTGW_TRUST_ROUTER_UNBOUND_SESSION) return TEE_ERROR_ACCESS_DENIED;
			return cmd_get_nworkers(sess_ctx, param_types, params);
		}

		case BSTGW_PTA_TRUST_ROUTER_GET_STATS: {
			DMSG("GET_STATUS got picked");
			if (sess->type != BSTGW_TRUST_ROUTER_UNBOUND_SESSION) return TEE_ERROR_ACCESS_DENIED;
			return bstgw_get_some_stats(param_types, params);
		}

		/* NPF Cleaner Thread */
		case BSTGW_PTA_TRUST_ROUTER_CMD_RUN_CLEANER: {
			if (sess->type != BSTGW_TRUST_ROUTER_CLEANER_SESSION) return TEE_ERROR_ACCESS_DENIED;
			if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
			return cmd_run_cleaner(sess);
		}

		/* Router's Worker Threads */
		case BSTGW_PTA_TRUST_ROUTER_CMD_RUN_WORKER: {
			if (sess->type != BSTGW_TRUST_ROUTER_WORKER_SESSION) return TEE_ERROR_ACCESS_DENIED;
			if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
			return cmd_run_worker(sess);
		}

        default:
            break;
	}

	return TEE_ERROR_NOT_IMPLEMENTED;
}

static TEE_Result
bstgw_called_by_trusted_app(const TEE_UUID *ref_ta_uuid) {
	if(!ref_ta_uuid) return TEE_ERROR_BAD_PARAMETERS;
	struct tee_ta_session *s = NULL;

	/* Check that we're called from a user TA */
	s = tee_ta_get_calling_session();
	if (!s) return TEE_ERROR_ACCESS_DENIED;
	if (!is_user_ta_ctx(s->ctx)) return TEE_ERROR_ACCESS_DENIED;

	/* Check against reference TA UUID */
	if (memcmp(&s->ctx->uuid, ref_ta_uuid, sizeof(TEE_UUID)))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

int bstgw_npfctl_cmd(uint64_t op, nvlist_t *req, nvlist_t *resp) {
	assert(req && resp);
	if(__predict_false( !trusted_router || !trusted_router->npf )) {
		EMSG("Trusted Router + Firewall not completely initialized, yet. Cannot call configuration interface.");
		return -1;
	}

	// NEW: netif information query
	if (op == IOC_BSTGW_QUERY_NETIFS) {
		DMSG("New IOC_BSTGW_QUERY_NETIFS command");
		nvlist_add_number(resp, "version", NPF_VERSION);
		nvlist_t *descr_list = ifnet_get_descr_list();
		if(!descr_list) {
			nvlist_add_number(resp, "errno", -1); //todo: errno?
			return -1;
		}

		nvlist_move_nvlist(resp, "ifs_list", descr_list);
		if(nvlist_error(resp)) {
			EMSG("Error while crafting config response");
			nvlist_add_number(resp, "errno", -1);
			return -1;
		}

		nvlist_add_number(resp, "errno", 0);
	} else {
		(void)npfctl_run_op(trusted_router->npf, op, req, resp);
	}
	return 0;
}

static TEE_Result bstgw_firewall_config(uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]) {
	// TODO: ConfHub TA makes more sense if intermediate FW language would be added
	//const TEE_UUID ta_ref_uuid = BSTGW_TA_CONF_HUB_UUID;
	const TEE_UUID ta_ref_uuid = BSTGW_TA_WEB_STUB_UUID;
	TEE_Result res;
	if( (res = bstgw_called_by_trusted_app(&ta_ref_uuid)) != TEE_SUCCESS ) {
		EMSG("Invalid call to firewall configuration interface");
		return res;
	}

	const uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_INPUT,  // req
		TEE_PARAM_TYPE_MEMREF_OUTPUT, // resp
		TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

    /* actual operation */
	const void *req_buf = params[0].memref.buffer;
	uint32_t req_buf_len = params[0].memref.size;

	DMSG("bstgw_firewall_config() got called with buffer: %p of len %u", req_buf, req_buf_len);

	/* Parse/Unpack request */
	nvlist_t *req = nvlist_unpack(req_buf, req_buf_len, 0);
	if(__predict_false( !req )) {
		EMSG("Malformed configuration request");
		return TEE_ERROR_BAD_FORMAT;
	}
	DMSG("Request dump:");
	nvlist_dump_ta(req);

	/* Pass configuration request to NPF firewall */
	nvlist_t *resp = nvlist_create(0);
	if(__predict_false( !resp )) {
		EMSG("OOM during configuration request");
		nvlist_destroy(req);
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	uint64_t op = dnvlist_get_number(req, "operation", UINT64_MAX);
	if( bstgw_npfctl_cmd(op, req, resp) != 0 ) {
		EMSG("Firewall configuration failed");
		nvlist_destroy(req);
		nvlist_destroy(resp);
		return TEE_ERROR_GENERIC;
	}
	DMSG("Response dump:");
	nvlist_dump_ta(resp);
	nvlist_destroy(req);

	/* Pack response */
	size_t packed_resp_len;
	void *packed_resp = nvlist_pack(resp, &packed_resp_len);
	if (__predict_false( !packed_resp )) {
		EMSG("Out of memory during configuration reply packing");
		nvlist_destroy(resp);
		return TEE_ERROR_OUT_OF_MEMORY;
	}

	/* Copy packed response into result buffer */
	// TODO: Out buffers are also provided by the caller, right?
	void *resp_buf = params[1].memref.buffer;
	uint32_t resp_buf_len = params[1].memref.size;
	if(__predict_false( resp_buf_len < packed_resp_len )) {
		EMSG("Output buffer is smaller than response (%u vs. %u)", resp_buf_len, packed_resp_len);
		free(packed_resp);
		nvlist_destroy(resp);
		return TEE_ERROR_SHORT_BUFFER;
	}

	memcpy(resp_buf, packed_resp, packed_resp_len);
	params[1].memref.size = packed_resp_len;

	// Done
	free(packed_resp);
	nvlist_destroy(resp);

    return TEE_SUCCESS;
}

static TEE_Result cmd_get_nworkers(wrk_sess_t *s, uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]) {
	(void)s;
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
	if (param_types != exp_param_types) return TEE_ERROR_BAD_PARAMETERS;

	params[0].value.a = worker_get_number();
    return TEE_SUCCESS;
}

#include <drivers/bstgw/imx_fec.h>
#include <bstgw/vnic-mmio.h>
static TEE_Result bstgw_get_some_stats(uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS]) {
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
	if (param_types != exp_param_types) return TEE_ERROR_BAD_PARAMETERS;

	//max_tx_complete; //bd_below_burst; //full_bd_ring; //full_nw_recv;
	params[0].value.a = 0;
    return TEE_SUCCESS;
}

static TEE_Result cmd_run_cleaner(wrk_sess_t *s __unused) {
	npfworker_run();
    return TEE_SUCCESS;
}

static TEE_Result cmd_run_worker(wrk_sess_t *rtr_sess) {
	(void)worker_run(rtr_sess->worker.router_worker);
    return TEE_SUCCESS;	
}

pseudo_ta_register(.uuid = BSTGW_PTA_TRUST_ROUTER_UUID, .name = PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS | TA_FLAG_CONCURRENT,
		   .open_session_entry_point = open_session,
		   .close_session_entry_point = close_session,
		   .invoke_command_entry_point = invoke_command);
