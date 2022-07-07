#include <drivers/bstgw/nw_mmio_traps.h>

#include <drivers/bstgw/imx_fec.h>
#include <drivers/bstgw/sw_fec_state.h>

#include <trace.h>

#include <dpdk/rte_ether.h>

//#define BSTGW_FEC_NW_TRAPS


static inline void fec_state_panic(sfec_state_t *st, paddr_t offset, uint32_t val, const char *reason) {
	EMSG("pri: %#x, sub: %#x; offset: %#lx, val: %#x",
		st->primary_st, st->sub_st, offset, val);
	if (reason) EMSG("reason: %s", reason);
	panic("Unexpected NW behaviour");
}

static bool
_fec_emulate_fec_restart(struct net_device *ndev, paddr_t offset,
	enum emu_state state, uint32_t *value, int size, bool sign) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	sfec_state_t *st = &fep->sfec_state;
	sfec_prist_t *pri_st = &st->primary_st;
	sfec_subst_t *sub_st = &st->sub_st;

	// todo: pri_st check

	assert (*sub_st >= SFEC_SUB_RESTART_RESET && 
		*sub_st <= SFEC_SUB_RESTART_IMASK_MII);
	//

	// we only expect NW writes, no reads
	if (state != EMU_STATE_WRITE)
		fec_state_panic(st, offset, *value, "only write allowed");

	switch (*sub_st)
	{
	case SFEC_SUB_RESTART_RESET:
		if (offset != FEC_IEVENT || *value != 0xffffffff)
			fec_state_panic(st, offset, *value, NULL);
		fec_restart_sw_1(ndev);
		*sub_st = SFEC_SUB_RESTART_INIT;
		break;

	case SFEC_SUB_RESTART_INIT:
		if (offset != FEC_X_CNTRL || (*value != 0x04 && *value != 0x0))
			fec_state_panic(st, offset, *value, NULL);
		fec_restart_sw_2(ndev);
		if (*value == 0x0) {
			st->rcntl |= 0x02;	
		}
		*sub_st = SFEC_SUB_RESTART_MII_MODE;
		break;

	case SFEC_SUB_RESTART_MII_MODE:
		if (offset != FEC_MII_SPEED)
			fec_state_panic(st, offset, *value, NULL);
		*sub_st = SFEC_SUB_RESTART_MII_SPEED;
		break;

	/* TODO: when do we need intermediate FCE_EN state? */
	case SFEC_SUB_RESTART_MII_SPEED:
	case SFEC_SUB_RESTART_FCE_EN:
		if (offset != FEC_R_FIFO_RSEM &&
			offset != FEC_R_CNTRL)
			fec_state_panic(st, offset, *value, NULL);

		/* to intermediate state */
		if (*sub_st == SFEC_SUB_RESTART_MII_SPEED &&
			offset == FEC_R_FIFO_RSEM) {
			if (*value != FEC_ENET_RSEM_V)
				fec_state_panic(st, offset, *value, "wrong write value");
			// fec_restart_sw_4_opt() is done on next access
			*sub_st = SFEC_SUB_RESTART_FCE_EN;
			break;
		}
		
		if (offset != FEC_R_CNTRL)
			fec_state_panic(st, offset, *value, NULL);
		
		fec_restart_sw_3(ndev);

		/* Enable flow control and length check */
		st->rcntl |= 0x40000000 | 0x00000020;

		// no intermediate state taken
		if (*sub_st == SFEC_SUB_RESTART_MII_SPEED) {
			st->rcntl &= ~FEC_ENET_FCE;
		} else if (*sub_st == SFEC_SUB_RESTART_FCE_EN) {
			st->rcntl |= FEC_ENET_FCE;
			fec_restart_sw_4_opt(ndev);
		} else {
			fec_state_panic(st, offset, *value, "BUG: impossible state");
		}

		// fixme: could filter valid combinations if desired
		*value &= (1 << 6) | (1 << 8) | (1 << 9);
		*value |= st->rcntl;
		st->rcntl = 0;
		*sub_st = SFEC_SUB_RESTART_R_CNTRL;
		break;

	case SFEC_SUB_RESTART_R_CNTRL:
		if (offset != FEC_ECNTRL)
			fec_state_panic(st, offset, *value, NULL);
		fec_restart_sw_5(ndev);
		*value &= (1 << 5);
		*value |= st->ecntl;
		st->ecntl = 0;
		*sub_st = SFEC_SUB_RESTART_ECNTRL;
		break;

	case SFEC_SUB_RESTART_ECNTRL:
		if (offset != FEC_IMASK || (*value != FEC_DEFAULT_IMASK
				&& *value != FEC_ENET_MII))
			fec_state_panic(st, offset, *value, NULL);
		
		fec_restart_sw_6(ndev);

		// different depending on *value
		if (*pri_st == SFEC_PRI_OPENING) {
			*pri_st = SFEC_PRI_POST_OPEN;
			if (*value == FEC_DEFAULT_IMASK)
				*sub_st = SFEC_SUB_RESTART_IMASK_RUN;
			else
				*sub_st = SFEC_SUB_RESTART_IMASK_MII;
		} else {
			*pri_st = SFEC_PRI_RUNNING;
			*sub_st = SFEC_SUB_NONE;
			if (*value != FEC_DEFAULT_IMASK)
				fec_state_panic(st, offset, *value, "should run, but wrong imask");
			fec_enet_adjust_link_sw_restart_post(ndev);
		}
		break;

	default:
		EMSG("unexpectedly reached default case");
		fec_state_panic(st, offset, *value, NULL);
		break;
	}
	//
	return true; // else we currently panic
}

static bool
_fec_emulate_fec_stop(struct net_device *ndev, paddr_t offset,
	enum emu_state state, uint32_t *value, int size, bool sign) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	sfec_state_t *st = &fep->sfec_state;
	sfec_prist_t *pri_st = &st->primary_st;
	sfec_subst_t *sub_st = &st->sub_st;

	// todo: pri_st check

	assert (*sub_st >= SFEC_SUB_STOP_GET_RMII &&
		*sub_st <= SFEC_SUB_STOP_ECNTRL);
	//
	switch (*sub_st)
	{
	case SFEC_SUB_STOP_GET_RMII:
	case SFEC_SUB_STOP_GET_IEVENT:
		if (state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, "expected write");

		// todo: fep->link might not be there on a shutdown (todo!)

		/* fep->link */
		if (offset == FEC_X_CNTRL && *value == 1 &&
			*sub_st == SFEC_SUB_STOP_GET_RMII) {
			*sub_st = SFEC_SUB_STOP_XMIT_STOP;
			break;
		}

		if (offset != FEC_ECNTRL || *value != 1)
			fec_state_panic(st, offset, *value, NULL);

		*sub_st = SFEC_SUB_STOP_RESET;
		break;

	case SFEC_SUB_STOP_XMIT_STOP:
		if (offset != FEC_IEVENT || state == EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, "expected read to FEC_IEVENT");
		if (state == EMU_STATE_READ_BEFORE) return true;

		*value &= FEC_ENET_GRA;
		*sub_st = SFEC_SUB_STOP_GET_IEVENT;
		break;

	case SFEC_SUB_STOP_RESET:
		if (offset != FEC_IMASK || *value != FEC_ENET_MII ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);
		*sub_st = SFEC_SUB_STOP_MII_IMASK;
		break;

	case SFEC_SUB_STOP_MII_IMASK:
		if (offset != FEC_MII_SPEED ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);
		*sub_st = SFEC_SUB_STOP_MII_SPEED;
		break;

	// todo: do operation in SW instead
	case SFEC_SUB_STOP_MII_SPEED:
		if (offset != FEC_ECNTRL || *value != 2 ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);
		*sub_st = SFEC_SUB_STOP_ECNTRL;
		break;

	case SFEC_SUB_STOP_ECNTRL:
		if (offset != FEC_R_CNTRL ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);

		if (*value != st->rmii_mode)
			fec_state_panic(st, offset, *value, "rmii_mode different");

		st->rmii_mode = 0;
		fec_enet_adjust_link_sw_stop_post(ndev);
		if (*pri_st == SFEC_PRI_SHUTTING_DOWN) {
			*pri_st = SFEC_PRI_SHUT_DOWN;
		} else {
			*pri_st = SFEC_PRI_STOPPED;
		}
		*sub_st = SFEC_SUB_NONE;
		break;

	default:
		EMSG("unexpectedly reached default case");
		fec_state_panic(st, offset, *value, NULL);
		break;
	}
	//
	return true; // else we currently panic
}


bool fec_emu_check(struct region *region, paddr_t address, enum emu_state state, uint32_t *value, int size, bool sign) {
	struct net_device *ndev = region->user_data;
	struct fec_enet_private *fep = netdev_priv(ndev);
	paddr_t offset = address - region->base;

#ifdef BSTGW_FEC_NW_TRAPS
	DMSG("fec_emu_check() %s access to addr: %#lx (offset: %#lx)",
		((state == EMU_STATE_WRITE) ? "write" : "read"), address, offset);
#endif

	sfec_state_t *st = &fep->sfec_state;
	sfec_prist_t *pri_st = &st->primary_st;
	sfec_subst_t *sub_st = &st->sub_st;

	bool allowed = true;

	// we assume 32bit size everywhere atm.
	if (size != 4) {
		EMSG("Unexpected access width: %u", size);
		return false;
	}

	// TODO: what about `sign`?

	/* If we are in error or shutdown state,
	 * don't allow any NW access */
	if (*pri_st == SFEC_PRI_ERROR  ||
		*pri_st == SFEC_PRI_SHUT_DOWN) {
		EMSG("NW access while SW is in error/uninit/shutdown state: block");
		return false;
	}

	//todo (fec_enet_mii_init)
	// TODO: does it happen? there is a case where it doesn't
	if (*pri_st == SFEC_PRI_PROBED &&
		offset == FEC_MII_SPEED &&
		state == EMU_STATE_WRITE) {
		IMSG("fec_enet_mii_init()");
		return true;
	}

	/* r/w by mdio CBs (TODO: when valid? distinguish from fec_stop IEVENT?) */
	if (offset == FEC_MII_DATA) {
		if (state == EMU_STATE_WRITE || state == EMU_STATE_READ_BEFORE)
			return true;
		*value = FEC_MMFR_DATA(*value);
		return true;
	}
// FIXME: better distinguish from IEVENT read in fec_stop()
	if (offset == FEC_IEVENT && state != EMU_STATE_WRITE &&
		*sub_st != SFEC_SUB_STOP_XMIT_STOP) {
		if (state == EMU_STATE_READ_BEFORE) return true;
		*value &= FEC_ENET_MII;
		return true;
	}

	switch (*pri_st)
	{
	// TODO: mostly code duplication
	case SFEC_PRI_PROBED:
		if (*sub_st != SFEC_SUB_NONE ||
			offset != FEC_ECNTRL || *value != 1 ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);
		fec_restart_sw_0(ndev);
		*pri_st = SFEC_PRI_OPENING;
		*sub_st = SFEC_SUB_RESTART_RESET;
		break;

	case SFEC_PRI_RESTARTED:
	case SFEC_PRI_STOPPED:
	case SFEC_PRI_RUNNING:
		if (*sub_st == SFEC_SUB_NONE) {
			// NW's virtual shut down signal
			if (offset == FEC_X_WMRK &&
				*value == 48 &&
				state == EMU_STATE_WRITE) {

				*pri_st = SFEC_PRI_SHUTTING_DOWN;
				return false; // virtual mmio must not be committed
			}

			// fec_stop 1st operation
			if (offset == FEC_R_CNTRL) {
				if (state == EMU_STATE_READ_BEFORE) return true;
				if (state != EMU_STATE_READ_AFTER)
					fec_state_panic(st, offset, *value, "expected read");
				
				fec_enet_adjust_link_sw_stop_pre(ndev);

				*value &= (1 << 8);
				st->rmii_mode = *value;
				*sub_st = SFEC_SUB_STOP_GET_RMII;
				break;
			}

			// fec_restart 1st operation
			if (offset == FEC_ECNTRL && *value == 1 &&
				state == EMU_STATE_WRITE && *pri_st != SFEC_PRI_SHUTTING_DOWN) {
				// note: fec_restart() from adjust link!
				fec_enet_adjust_link_sw_restart_pre(ndev);
				fec_restart_sw_0(ndev);
				*sub_st = SFEC_SUB_RESTART_RESET;
				break;
			}

			fec_state_panic(st, offset, *value, "unexpected access");

		} else if (*sub_st >= SFEC_SUB_STOP_GET_RMII &&
			*sub_st <= SFEC_SUB_STOP_ECNTRL) {
			allowed = _fec_emulate_fec_stop(ndev, offset,
				state, value, size, sign);
		} else if (*pri_st == SFEC_PRI_SHUTTING_DOWN) {
			fec_state_panic(st, offset, *value, "non-stop operation while shutting down");
		} else if (*sub_st >= SFEC_SUB_RESTART_RESET &&
			*sub_st <= SFEC_SUB_RESTART_IMASK_MII) {
			allowed = _fec_emulate_fec_restart(ndev, offset,
			state, value, size, sign);
		} else {
			fec_state_panic(st, offset, *value, "unexpected access");
		}
		break;

	case SFEC_PRI_OPENING:
		if (*sub_st < SFEC_SUB_RESTART_RESET ||
			*sub_st > SFEC_SUB_RESTART_IMASK_MII)
			fec_state_panic(st, offset, *value, NULL);

		allowed = _fec_emulate_fec_restart(ndev, offset,
			state, value, size, sign);
		break;

	case SFEC_PRI_POST_OPEN:
		if (offset != FEC_X_WMRK || *value != 47 ||
			state != EMU_STATE_WRITE)
			fec_state_panic(st, offset, *value, NULL);

		switch (*sub_st)
		{
		case SFEC_SUB_RESTART_IMASK_RUN:
			*pri_st = SFEC_PRI_RUNNING;
			break;

		case SFEC_SUB_RESTART_IMASK_MII:
			*pri_st = SFEC_PRI_RESTARTED;
			break;

		default:
			fec_state_panic(st, offset, *value, NULL);
		}
		*sub_st = SFEC_SUB_NONE;
		// here, bcs. maybe mdio CB could fire DA, so have state updated
		fec_enet_open_sw(ndev);
		return false; // virtual mmio must not be committed

	default:
		panic("Invalid FEC primary state");
	}

	return allowed;
}
