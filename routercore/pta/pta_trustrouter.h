#ifndef __BSTGW_PTA_TRUST_ROUTER_H
#define __BSTGW_PTA_TRUST_ROUTER_H

#define BSTGW_PTA_TRUST_ROUTER_UUID { 0x0d976d02, 0xc3d0, 0x4709, \
		{ 0x9e, 0x41, 0xdb, 0xe5, 0xea, 0xbd, 0x4f, 0x40} }

/* Session Types */
#define BSTGW_TRUST_ROUTER_UNBOUND_SESSION 	0
#define BSTGW_TRUST_ROUTER_CLEANER_SESSION 	1
#define BSTGW_TRUST_ROUTER_WORKER_SESSION 	2



/* Commands */
/*
 * BSTGW_PTA_TRUST_ROUTER_CMD_FW_CONFIG - Submit npfctl nvlist object.
 * param[0] (memref_input) packed nvlist object
 * param[1] (memref_output) packed response nvlist object
 * param[2] (unused)
 * param[3] (unused)
 */
#define BSTGW_PTA_TRUST_ROUTER_CMD_CONFIG	0

/*
 * BSTGW_PTA_TRUST_ROUTER_CMD_GET_NWORKERS - Returns number of supported worker threads.
 * param[0] (value_output) number
 * param[1-3] (unused)
 */
#define BSTGW_PTA_TRUST_ROUTER_CMD_GET_NWORKERS		1

// BSTGW_PTA_TRUST_ROUTER_CMD_RUN_CLEANER/WORKER - Run working loop / iteration (TODO!) of cleaner/worker thread.
#define BSTGW_PTA_TRUST_ROUTER_CMD_RUN_CLEANER 		2
#define BSTGW_PTA_TRUST_ROUTER_CMD_RUN_WORKER 		3


/* Utils (unbound session) */

/*
 * BSTGW_PTA_TRUST_ROUTER_GET_STATS - Returns number of x.
 * param[0] (value_output) #x
 * param[1-3] (unused)
 */
#define BSTGW_PTA_TRUST_ROUTER_GET_STATS 4

#endif /* __BSTGW_PTA_TRUST_ROUTER_H */
