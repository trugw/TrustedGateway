#ifndef BSTGW_SW_FEC_STATE_H
#define BSTGW_SW_FEC_STATE_H

typedef enum {
	/* error / pre-init */
	SFEC_PRI_ERROR = -1,			// error
	SFEC_PRI_UNINIT = 0,			// not yet initialized
	
	/* waiting for NW */
	SFEC_PRI_PROBED,		// 0x1
	SFEC_PRI_OPENING,		// 0x2
	SFEC_PRI_POST_OPEN,		// 0x3

	/* runtime */
	SFEC_PRI_RESTARTED, 	// 0x4	// mii still active
	SFEC_PRI_STOPPED,   	// 0x5	// mii still active
	SFEC_PRI_RUNNING,		// 0x6

	/* terminated */
	SFEC_PRI_SHUTTING_DOWN, // 0x7
	SFEC_PRI_SHUT_DOWN		// 0x8

} sfec_prist_t;

typedef enum {
	SFEC_SUB_NONE = 0,

	/* fec_restart */
	SFEC_SUB_RESTART_RESET = 0x10,
	SFEC_SUB_RESTART_INIT,			// 0x11
	SFEC_SUB_RESTART_MII_MODE,		// 0x12
	SFEC_SUB_RESTART_MII_SPEED,		// 0x13
	SFEC_SUB_RESTART_FCE_EN, // todo! (skipped under certain conditions)
	SFEC_SUB_RESTART_R_CNTRL,		// 0x14
	SFEC_SUB_RESTART_ECNTRL,		// 0x15
	SFEC_SUB_RESTART_IMASK_RUN,		// 0x16 	// fep->link
	SFEC_SUB_RESTART_IMASK_MII,		// 0x17 	// !fep->link

	/* fec_stop */
	SFEC_SUB_STOP_GET_RMII = 0x20,
	SFEC_SUB_STOP_XMIT_STOP,		// 0x21		// skipped iff (!fep->link)
	SFEC_SUB_STOP_GET_IEVENT,		// 0x22 	// skipped iff (!fep->link)
	SFEC_SUB_STOP_RESET,			// 0x23
	SFEC_SUB_STOP_MII_IMASK,		// 0x24
	SFEC_SUB_STOP_MII_SPEED,		// 0x25
	SFEC_SUB_STOP_ECNTRL,			// 0x26
	//SFEC_SUB_STOP_R_CNTRL,

} sfec_subst_t;

typedef struct bstgw_sec_fec_state {
	sfec_prist_t primary_st;
	sfec_subst_t sub_st;
	uint32_t rcntl, ecntl, rmii_mode;
} sfec_state_t;

#endif /* BSTGW_SW_FEC_STATE_H */