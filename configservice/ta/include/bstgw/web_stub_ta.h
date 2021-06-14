#ifndef BSTGW_TA_WEB_STUB_H
#define BSTGW_TA_WEB_STUB_H

#define BSTGW_TA_WEB_STUB_UUID \
	{ 0x0b9d11c7, 0x2281, 0x4b24, \
		{ 0xb5, 0xf0, 0x6c, 0x15, 0xe1, 0x08, 0x85, 0xfb} }

/* Session Types */
#define BSTGW_WEBSTUB_SERVER_SESSION 	0
#define BSTGW_WEBSTUB_CLIENT_SESSION 	1

/* The function IDs implemented in this TA */

/* ************************ */
/* ** Server Session API ** */
/* ************************ */

/*
 * BSTGW_TA_WEB_STUB_CMD_INIT_HTTPS_SERVER - Allocate mbedTLS resources for the server
 * All parameters unused.
 */
//TODO: auth-encrypted priv. key ++ cert. as input? public client CA cert?
#define BSTGW_TA_WEB_STUB_CMD_INIT_HTTPS_SERVER 	0

/*
 * BSTGW_TA_WEB_STUB_CMD_FINI_HTTPS_SERVER - Free mbedTLS resources of the server
 * All parameters unused.
 */
#define BSTGW_TA_WEB_STUB_CMD_FINI_HTTPS_SERVER		1

/*
 * BSTGW_TA_WEB_STUB_CMD_LOAD_WEBAPP_FILE - Simplified file bootstrapping (w/o OCALLs)
 * param[0] (memref_input) File buffer
 * param[1] (value_input) File ID
 * param[2] (memref_input) Signature over hash(str(File ID)|File Buffer)
 * param[3] (unused)
 */
#define BSTGW_TA_WEB_STUB_CMD_LOAD_WEBAPP_FILE		2

/* ************************ */
/* ** Client Session API ** */
/* ************************ */

/*
 * BSTGW_TA_WEB_STUB_CMD_INIT_TLS_CLIENT - Allocate mbedTLS resources for the client
 * All parameters unused.
 */
//TODO: link to server config?! (or via singleton TA + global FSM?)
#define BSTGW_TA_WEB_STUB_CMD_INIT_TLS_CLIENT 		10

/*
 * BSTGW_TA_WEB_STUB_CMD_FINI_TLS_CLIENT - Free mbedTLS resources of the client
 * All parameters unused.
 */
#define BSTGW_TA_WEB_STUB_CMD_FINI_TLS_CLIENT		11

/*
 * BSTGW_TA_WEB_STUB_CMD_INIT_HTTP_CLI - Allocate mbedTLS resources for the client
 * All parameters unused.
 */
// TODO: consider dropping and moving into SW-only
#define BSTGW_TA_WEB_STUB_CMD_INIT_HTTP_CLI 		12

/*
 * BSTGW_TA_WEB_STUB_CMD_FINI_HTTP_CLI - Free mbedTLS resources of the client
 * All parameters unused.
 */
// TODO: consider dropping and moving into SW-only
#define BSTGW_TA_WEB_STUB_CMD_FINI_HTTP_CLI			13

/*
 * BSTGW_TA_WEB_STUB_CMD_DO_HANDSHAKE - Performs mbedTLS handshake step for the client's TLS connection
 * param[0] (value_output) a: mbedTLS return value, b: unused
 * param[1] (memref_inout) Current RX ringbuffer
 * param[2] (memref_inout) Current TX ringbuffer
 * param[3] (unused)
 */
#define BSTGW_TA_WEB_STUB_CMD_DO_HANDSHAKE			14

/*
 * BSTGW_TA_WEB_STUB_CMD_PROCESS_REQUEST - Handles currently pending HTTP requests
 * param[0] (value_output) a: mbedTLS return value, b: unused
 * param[1] (memref_inout) Current RX ringbuffer
 * param[2] (memref_inout) Current TX ringbuffer
 * param[3] (unused)
 */
#define BSTGW_TA_WEB_STUB_CMD_PROCESS_REQUEST		15

enum client_type { USER, ADMIN, MASTER, PENDING, INVALID };

#endif /*BSTGW_TA_WEB_STUB_H*/
