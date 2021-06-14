#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>

#include <bstgw/web_stub_ta.h>
#include "secure_ta.h"
#include "sw_fileio.h"
#include <bstgw/ringbuffer.h>

#include <assert.h>

// TODO: global single-instance; (note: FSM would make sense here as well!)
struct web_stub_state {
	const struct mbedtls_ssl_config *server_config;
};

static struct web_stub_state *webstub_ta; // global instance state

typedef struct sess_info {
	int type;
	union {
		struct sw_server_session *srv;
		struct sw_client_session *cli;
	};
} sess_t;

static TEE_Result cmd_init_https_server(sess_t *srv_sess);
static TEE_Result cmd_fini_https_server(sess_t *srv_sess);

static TEE_Result cmd_init_tls_client(sess_t *cli_sess);
static TEE_Result cmd_fini_tls_client(sess_t *cli_sess);

static TEE_Result cmd_init_http_cli(sess_t *cli_sess);
static TEE_Result cmd_fini_http_cli(sess_t *cli_sess);

static TEE_Result cmd_do_handshake(sess_t *cli_sess, uint32_t param_types, TEE_Param params[4]);
static TEE_Result cmd_process_request(sess_t *cli_sess, uint32_t param_types, TEE_Param params[4]);

static TEE_Result cmd_load_webapp_file(sess_t *cli_sess, uint32_t param_types, TEE_Param params[4]);

/*
 * Called when the instance of the TA is created. This is the first call in
 * the TA.
 */
TEE_Result TA_CreateEntryPoint(void)
{
	DMSG("has been called");
	if( (webstub_ta = TEE_Malloc(sizeof(struct web_stub_state), TEE_MALLOC_FILL_ZERO)) == NULL ) {
		EMSG("OOM");
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	return TEE_SUCCESS;
}

/*
 * Called when the instance of the TA is destroyed if the TA has not
 * crashed or panicked. This is the last call in the TA.
 */
void TA_DestroyEntryPoint(void)
{
	DMSG("has been called");
	TEE_Free(webstub_ta);
}

/*
 * Called when a new session is opened to the TA. *sess_ctx can be updated
 * with a value to be able to identify this session in subsequent calls to the
 * TA. In this function you will normally do the global initialization for the
 * TA.
 */
TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
		TEE_Param params[4],
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
		case BSTGW_WEBSTUB_SERVER_SESSION:
		case BSTGW_WEBSTUB_CLIENT_SESSION:
			break;
		default:
			return TEE_ERROR_BAD_PARAMETERS;
	}

	/* Allocate client context */
	// note: _NO_FILL doesn't exist in OP-TEE (and 0 == FILL_ZERO)
	if( (*sess_ctx = TEE_Malloc(sizeof(struct sess_info), 0)) == NULL) {
		EMSG("OOM");
		return TEE_ERROR_OUT_OF_MEMORY;
	}
	((struct sess_info *)(*sess_ctx))->type = sess_type;

	/*
	 * The DMSG() macro is non-standard, TEE Internal API doesn't
	 * specify any means to logging from a TA.
	 */
	IMSG("Open Session!\n");

	/* If return value != TEE_SUCCESS the session will not be created. */
	return TEE_SUCCESS;
}

/*
 * Called when a session is closed, sess_ctx hold the value that was
 * assigned by TA_OpenSessionEntryPoint().
 */
void TA_CloseSessionEntryPoint(void *sess_ctx)
{
	if (sess_ctx != NULL) TEE_Free(sess_ctx);
	else { EMSG("Warning: close session did not cause a memory free"); }
	IMSG("Goodbye!\n");
}

/*
 * Called when a TA is invoked. sess_ctx hold that value that was
 * assigned by TA_OpenSessionEntryPoint(). The rest of the paramters
 * comes from normal world.
 */
TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx,
			uint32_t cmd_id,
			uint32_t param_types, TEE_Param params[4])
{
	if (sess_ctx == NULL) {
		EMSG("Why is *sess_ctx NULL?!");
		return TEE_ERROR_GENERIC;
	}
	sess_t *sess = sess_ctx;

	uint32_t no_params_type = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE,
		TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);

	switch (cmd_id) {
	/* Server Session API */
	case BSTGW_TA_WEB_STUB_CMD_INIT_HTTPS_SERVER:
		if (sess->type != BSTGW_WEBSTUB_SERVER_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_init_https_server(sess);

	case BSTGW_TA_WEB_STUB_CMD_FINI_HTTPS_SERVER:
		if (sess->type != BSTGW_WEBSTUB_SERVER_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_fini_https_server(sess);

	case BSTGW_TA_WEB_STUB_CMD_LOAD_WEBAPP_FILE:
		if (sess->type != BSTGW_WEBSTUB_SERVER_SESSION) return TEE_ERROR_ACCESS_DENIED;
		return cmd_load_webapp_file(sess, param_types, params);

	/* Client Session API */
	case BSTGW_TA_WEB_STUB_CMD_INIT_TLS_CLIENT:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_init_tls_client(sess);

	case BSTGW_TA_WEB_STUB_CMD_FINI_TLS_CLIENT:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_fini_tls_client(sess);

	case BSTGW_TA_WEB_STUB_CMD_INIT_HTTP_CLI:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_init_http_cli(sess);

	case BSTGW_TA_WEB_STUB_CMD_FINI_HTTP_CLI:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		if (param_types != no_params_type) return TEE_ERROR_BAD_PARAMETERS;
		return cmd_fini_http_cli(sess);

	case BSTGW_TA_WEB_STUB_CMD_DO_HANDSHAKE:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		return cmd_do_handshake(sess, param_types, params);

	case BSTGW_TA_WEB_STUB_CMD_PROCESS_REQUEST:
		if (sess->type != BSTGW_WEBSTUB_CLIENT_SESSION) return TEE_ERROR_ACCESS_DENIED;
		return cmd_process_request(sess, param_types, params);

	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}

static TEE_Result cmd_init_https_server(sess_t *srv_sess) {
	DMSG("has been called");
	if (webstub_ta->server_config != NULL) {
		EMSG("Server Config is already defined!");
		return TEE_ERROR_GENERIC;
	}

	if(0 != sw_init_tls_srv(&srv_sess->srv)) return TEE_ERROR_GENERIC;

	// save server config ref
	webstub_ta->server_config = get_tls_srv_conf(srv_sess->srv);
	assert(webstub_ta->server_config != NULL);
	return TEE_SUCCESS;
}
static TEE_Result cmd_fini_https_server(sess_t *srv_sess) {
	DMSG("has been called");
	if(0 != sw_fini_tls_srv(srv_sess->srv)) {
		webstub_ta->server_config = NULL; // TODO: reset server config?!
		return TEE_ERROR_GENERIC;
	}

	// reset server config ref
	webstub_ta->server_config = NULL;
	return TEE_SUCCESS;
}

static TEE_Result cmd_init_tls_client(sess_t *cli_sess) {
	DMSG("has been called");
	if (webstub_ta->server_config == NULL) {
		EMSG("No Server Config available!");
		return TEE_ERROR_GENERIC;
	}

	if(0 != sw_init_tls_cli(&cli_sess->cli, webstub_ta->server_config)) return TEE_ERROR_GENERIC;
	return TEE_SUCCESS;
}
static TEE_Result cmd_fini_tls_client(sess_t *cli_sess) {
	DMSG("has been called");
	if(0 != sw_fini_tls_cli(cli_sess->cli)) return TEE_ERROR_GENERIC;
	return TEE_SUCCESS;
}

static TEE_Result cmd_init_http_cli(sess_t *cli_sess) {
	DMSG("has been called");
	if(0 != sw_init_http_cli(cli_sess->cli)) return TEE_ERROR_GENERIC;
	return TEE_SUCCESS;
}
static TEE_Result cmd_fini_http_cli(sess_t *cli_sess) {
	DMSG("has been called");
	if(0 != sw_fini_http_cli(cli_sess->cli)) return TEE_ERROR_GENERIC;
	return TEE_SUCCESS;
}

static TEE_Result ringbuffer_param_check(uint32_t param_types, TEE_Param params[4]) {
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_VALUE_OUTPUT, // extra return value
		TEE_PARAM_TYPE_MEMREF_INOUT, // rx ringbuffer
		TEE_PARAM_TYPE_MEMREF_INOUT, // tx ringbuffer
		TEE_PARAM_TYPE_NONE);

	if (param_types != exp_param_types) return TEE_ERROR_BAD_PARAMETERS;

	/* At the moment we naively assume that the whole ringbuffer struct is passed */
	if (params[1].memref.size < sizeof(sw_buff_t) || params[1].memref.buffer == NULL
		|| params[2].memref.size < sizeof(sw_buff_t) || params[2].memref.buffer == NULL)
		return TEE_ERROR_BAD_PARAMETERS;
	
	return TEE_SUCCESS;
}

static TEE_Result cmd_do_handshake(sess_t *cli_sess, uint32_t param_types, TEE_Param params[4]) {
	DMSG("has been called");
	TEE_Result ret = ringbuffer_param_check(param_types, params);
	if (ret != TEE_SUCCESS) {
		EMSG("Invalid ringbuffer parameters");
		return ret;
	}

	if (set_rxtx_buffers(cli_sess->cli, params[1].memref.buffer, params[2].memref.buffer) != 0) {
		EMSG("Setting rxtx buffers failed");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	params[0].value.a = sw_do_handshake_cli(cli_sess->cli); // TODO: 64 bit vs. 32 bit !!
	clear_rxtx_buffers(cli_sess->cli);
	
	return TEE_SUCCESS;
}
static TEE_Result cmd_process_request(sess_t *cli_sess, uint32_t param_types, TEE_Param params[4]) {
	DMSG("has been called");
	TEE_Result ret = ringbuffer_param_check(param_types, params);
	if (ret != TEE_SUCCESS) {
		EMSG("Invalid ringbuffer parameters");
		return ret;
	}

	if (set_rxtx_buffers(cli_sess->cli, params[1].memref.buffer, params[2].memref.buffer) != 0) {
		EMSG("Setting rxtx buffers failed");
		return TEE_ERROR_BAD_PARAMETERS;
	}
	params[0].value.a = sw_read_https_req_data(cli_sess->cli); // TODO: 64 bit vs. 32 bit !!
	clear_rxtx_buffers(cli_sess->cli);

	return TEE_SUCCESS;
}

static TEE_Result cmd_load_webapp_file(sess_t *srv_sess, uint32_t param_types, TEE_Param params[4]) {
	(void)srv_sess; // TODO?
	DMSG("has been called");
	uint32_t exp_param_types = TEE_PARAM_TYPES(
		TEE_PARAM_TYPE_MEMREF_INPUT, // file buffer
		TEE_PARAM_TYPE_VALUE_INPUT,  // file ID
		TEE_PARAM_TYPE_MEMREF_INPUT, // signature over hash(type|content)
		TEE_PARAM_TYPE_NONE);
	if (param_types != exp_param_types) return TEE_ERROR_BAD_PARAMETERS;
	if (sw_init_webapp_file(params[0].memref.buffer, params[0].memref.size, params[1].value.a,
						params[2].memref.buffer, params[2].memref.size) != 0) return TEE_ERROR_GENERIC;
	return TEE_SUCCESS;
}