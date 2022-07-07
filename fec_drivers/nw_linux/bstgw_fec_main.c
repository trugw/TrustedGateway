/*
 * Fast Ethernet Controller (FEC) driver for Motorola MPC8xx.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * Right now, I am very wasteful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.
 *
 * Much better multiple PHY support by Magnus Damm.
 * Copyright (c) 2000 Ericsson Radio Systems AB.
 *
 * Support for FEC controller of ColdFire processors.
 * Copyright (c) 2001-2005 Greg Ungerer (gerg@snapgear.com)
 *
 * Bug fixes and cleanup by Philippe De Muyter (phdm@macqel.be)
 * Copyright (c) 2004-2006 Macq Electronique SA.
 *
 * Copyright (C) 2010-2014 Freescale Semiconductor, Inc.
 *
 * Copyright 2017-2018 NXP
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/tso.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/clk/clk-conf.h>
#include <linux/platform_device.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/fec.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/regulator/consumer.h>
#include <linux/if_vlan.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/busfreq-imx.h>
#include <linux/prefetch.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <asm/cacheflush.h>
#include <soc/imx/cpuidle.h>

#include "bstgw_fec.h"

#define DRIVER_NAME	"fec_bstgw"

static const u16 fec_enet_vlan_pri_to_queue[8] = {1, 1, 1, 1, 2, 2, 2, 2};

/* Pause frame feild and FIFO threshold */
#define FEC_ENET_FCE	(1 << 5)
#define FEC_ENET_RSEM_V	0x84
#define FEC_ENET_RSFL_V	16
#define FEC_ENET_RAEM_V	0x8
#define FEC_ENET_RAFL_V	0x8
#define FEC_ENET_OPD_V	0xFFF0
#define FEC_MDIO_PM_TIMEOUT  100 /* ms */

static struct platform_device_id fec_devtype[] = {
	{
		.name = "imx6q-fec",
		// TODO: maybe re-enable bufdesc_ex in case we want VLAN/CSUM;
		// 		 but then we also have to handle the PTP clock
		.driver_data = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
				/*FEC_QUIRK_HAS_BUFDESC_EX |*/ FEC_QUIRK_HAS_CSUM |
				FEC_QUIRK_HAS_VLAN | FEC_QUIRK_ERR006358 |
				FEC_QUIRK_HAS_RACC | FEC_QUIRK_BUG_WAITMODE,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, fec_devtype);

enum imx_fec_type {
	BSTGW_IMX6Q_FEC = 0,
};

static const struct of_device_id fec_dt_ids[] = {
	{ .compatible = "fsl,bstgw-imx6q-fec", .data = &fec_devtype[BSTGW_IMX6Q_FEC], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fec_dt_ids);

static int disable_giga=0;
module_param(disable_giga, int, 0664);
MODULE_PARM_DESC(disable_giga, "disable gigabit speeds");

/* The FEC stores dest/src/type/vlan, data, and checksum for receive packets.
 *
 * 2048 byte skbufs are allocated. However, alignment requirements
 * varies between FEC variants. Worst case is 64, so round down by 64.
 */
#define PKT_MAXBUF_SIZE		(round_down(2048 - 64, 64))
#define PKT_MINBUF_SIZE		64

/* FEC receive acceleration */
#define FEC_RACC_IPDIS		(1 << 1)
#define FEC_RACC_PRODIS		(1 << 2)
#define FEC_RACC_SHIFT16	BIT(7)
#define FEC_RACC_OPTIONS	(FEC_RACC_IPDIS | FEC_RACC_PRODIS)

/* MIB Control Register */
#define FEC_MIB_CTRLSTAT_DISABLE	BIT(31)

/*
 * RX control register also contains maximum frame
 * size bits.
 */
#define	OPT_FRAME_SIZE	(PKT_MAXBUF_SIZE << 16)

/* FEC MII MMFR bits definition */
#define FEC_MMFR_ST		(1 << 30)
#define FEC_MMFR_OP_READ	(2 << 28)
#define FEC_MMFR_OP_WRITE	(1 << 28)
#define FEC_MMFR_PA(v)		((v & 0x1f) << 23)
#define FEC_MMFR_RA(v)		((v & 0x1f) << 18)
#define FEC_MMFR_TA		(2 << 16)
#define FEC_MMFR_DATA(v)	(v & 0xffff)
/* FEC ECR bits definition */
#define FEC_ECR_MAGICEN		(1 << 2)
#define FEC_ECR_SLEEP		(1 << 3)

#define FEC_MII_TIMEOUT		30000 /* us */

/* Transmitter timeout */
#define TX_TIMEOUT (2 * HZ)

#define FEC_PAUSE_FLAG_AUTONEG	0x1
#define FEC_PAUSE_FLAG_ENABLE	0x2

/* By default, set the copybreak to 1518,
 * then the RX path always keep DMA memory unchanged, and
 * allocate one new skb and copy DMA memory data to the new skb
 * buffer, which can improve the performance when SMMU is enabled.
 *
 * The driver support .set_tunable() interface for ethtool, user
 * can dynamicly change the copybreak value.
 */
#define COPYBREAK_DEFAULT	1518

/* Max number of allowed TCP segments for software TSO */
#define FEC_MAX_TSO_SEGS	100
#define FEC_MAX_SKB_DESCS	(FEC_MAX_TSO_SEGS * 2 + MAX_SKB_FRAGS)

#define IS_TSO_HEADER(txq, addr) \
	((addr >= txq->tso_hdrs_dma) && \
	(addr < txq->tso_hdrs_dma + txq->bd.ring_size * TSO_HEADER_SIZE))

static int mii_cnt;

#if 0
static struct bufdesc *
fec_enet_txq_submit_frag_skb(struct fec_enet_priv_tx_q *txq,
			     struct sk_buff *skb,
			     struct net_device *ndev)
{
//				if (unlikely(skb_shinfo(skb)->tx_flags &
//					SKBTX_HW_TSTAMP && fep->hwts_tx_en))
//					estatus |= BD_ENET_TX_TS;
}

static int fec_enet_txq_submit_skb(struct fec_enet_priv_tx_q *txq,
				   struct sk_buff *skb, struct net_device *ndev)
{
//			if (unlikely(skb_shinfo(skb)->tx_flags &
//				SKBTX_HW_TSTAMP && fep->hwts_tx_en))
//				estatus |= BD_ENET_TX_TS;

//		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
//			fep->hwts_tx_en))
//			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}

#endif /* 0 */

/*
 * This function is called to start or restart the FEC during a link
 * change, transmit timeout, or to reconfigure the FEC.  The network
 * packet processing for this device must be stopped before this call.
 */
static void
fec_restart(struct net_device *ndev)
{
	/* primary_st == SFEC_PRI_PROBED | _RUNNING/STOPPED/RESTARTED
	 * sub_st	  == SFEC_SUB_NONE */
	struct fec_enet_private *fep = netdev_priv(ndev);
//sw	u32 val;
//sw	u32 temp_mac[2];
	u32 rcntl = 0; //sw: OPT_FRAME_SIZE | 0x04;
	u32 ecntl = 0; //sw: FEC_ENET_ETHEREN; /* ETHEREN */

	/* Whack a reset.  We should wait for this.
	 * For i.MX6SX SOC, enet use AXI bus, we use disable MAC
	 * instead of reset MAC itself.
	 */
	writel(1, fep->hwp + FEC_ECNTRL);
	/* #PRI_PROBED -> primary_st == SFEC_PRI_OPENING
	 * sub_st	  == SFEC_SUB_RESTART_RESET */
	udelay(10);

	/*
	 * enet-mac reset will reset mac address registers too,
	 * so need to reconfigure it.
	 */
#if 0 // sw (1)
	memcpy(&temp_mac, ndev->dev_addr, ETH_ALEN);
	writel((__force u32)cpu_to_be32(temp_mac[0]),
	       fep->hwp + FEC_ADDR_LOW);
	writel((__force u32)cpu_to_be32(temp_mac[1]),
	       fep->hwp + FEC_ADDR_HIGH);
#endif // sw

	/* Clear any outstanding interrupt. */
	writel(0xffffffff, fep->hwp + FEC_IEVENT);
	/* sub_st	  == SFEC_SUB_RESTART_INIT */

#if 0 // sw (2)
	fec_enet_bd_init(ndev);

	fec_enet_enable_ring(ndev);

	/* Reset tx SKB buffers. */
	fec_enet_reset_skb(ndev);
#endif

	/* Enable MII mode */
	if (fep->full_duplex == DUPLEX_FULL) {
		/* FD enable */
		writel(0x04, fep->hwp + FEC_X_CNTRL);
	} else {
		/* No Rcv on Xmit */
//sw:		rcntl |= 0x02;
		writel(0x0, fep->hwp + FEC_X_CNTRL);
	}
	/* sub_st	  == SFEC_SUB_RESTART_MII_MODE */

	/* Set MII speed */
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);
	/* sub_st	  == SFEC_SUB_RESTART_MII_SPEED */

#if 0 // sw (3)
	if (fep->quirks & FEC_QUIRK_HAS_RACC) {
		val = readl(fep->hwp + FEC_RACC);
		/* align IP header */
		val |= FEC_RACC_SHIFT16;
		if (fep->csum_flags & FLAG_RX_CSUM_ENABLED)
			/* set RX checksum */
			val |= FEC_RACC_OPTIONS;
		else
			val &= ~FEC_RACC_OPTIONS;
		writel(val, fep->hwp + FEC_RACC);
		writel(PKT_MAXBUF_SIZE, fep->hwp + FEC_FTRL);
	}
	writel(PKT_MAXBUF_SIZE, fep->hwp + FEC_FTRL);
#endif

	/*
	 * The phy interface and speed need to get configured
	 * differently on enet-mac.
	 */
	// always true
	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		/* Enable flow control and length check */
//sw:		rcntl |= 0x40000000 | 0x00000020;

		/* RGMII, RMII or MII */
		if (fep->phy_interface == PHY_INTERFACE_MODE_RGMII ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_ID ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_RXID ||
		    fep->phy_interface == PHY_INTERFACE_MODE_RGMII_TXID)
			rcntl |= (1 << 6);
		else if (fep->phy_interface == PHY_INTERFACE_MODE_RMII)
			rcntl |= (1 << 8);
		else
			rcntl &= ~(1 << 8);

		/* 1G, 100M or 10M */
		if (ndev->phydev) {
			if (ndev->phydev->speed == SPEED_1000)
				ecntl |= (1 << 5);
			else if (ndev->phydev->speed == SPEED_100)
				rcntl &= ~(1 << 9);
			else
				rcntl |= (1 << 9);
		}
	}

	/* enable pause frame*/
	// TODO: when are the phydev conditions true? (not always as it seems)
	if ((fep->pause_flag & FEC_PAUSE_FLAG_ENABLE) ||
	    ((fep->pause_flag & FEC_PAUSE_FLAG_AUTONEG) &&
	     ndev->phydev && ndev->phydev->pause)) {
//sw:		rcntl |= FEC_ENET_FCE;

		/* set FIFO threshold parameter to reduce overrun */
		writel(FEC_ENET_RSEM_V, fep->hwp + FEC_R_FIFO_RSEM);
		/* sub_st	== SFEC_SUB_RESTART_FCE_EN */
//sw		writel(FEC_ENET_RSFL_V, fep->hwp + FEC_R_FIFO_RSFL);
//sw		writel(FEC_ENET_RAEM_V, fep->hwp + FEC_R_FIFO_RAEM);
//sw		writel(FEC_ENET_RAFL_V, fep->hwp + FEC_R_FIFO_RAFL);

		/* OPD */
//sw		writel(FEC_ENET_OPD_V, fep->hwp + FEC_OPD);
	} else {
//sw:		rcntl &= ~FEC_ENET_FCE;
	}

	writel(rcntl, fep->hwp + FEC_R_CNTRL);
	/* sub_st	  == SFEC_SUB_RESTART_R_CNTRL */

#if 0 // sw (5)
	/* Setup multicast filter. */
	set_multicast_list(ndev);
	writel(0, fep->hwp + FEC_HASH_TABLE_HIGH);
	writel(0, fep->hwp + FEC_HASH_TABLE_LOW);

	// always true
	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		/* enable ENET endian swap */
		ecntl |= (1 << 8);
		/* enable ENET store and forward mode */
		writel(1 << 8, fep->hwp + FEC_X_WMRK);
	}

	if (fep->bufdesc_ex)
		ecntl |= (1 << 4);

	/* Enable the MIB statistic event counters */
	writel(0 << 31, fep->hwp + FEC_MIB_CTRLSTAT);
#endif

	/* And last, enable the transmit and receive processing */
	writel(ecntl, fep->hwp + FEC_ECNTRL);
	/* sub_st	  == SFEC_SUB_RESTART_ECNTRL */
//sw (6)	fec_enet_active_rxring(ndev);

	// disabled atm
	if (fep->bufdesc_ex)
		bstgw_fec_ptp_start_cyclecounter(ndev);

	/* Enable interrupts we wish to service */
	if (fep->link)
		writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK); // TXF || RXF || MII
		/* #PRI_OPENING 	-> sub_st	  == SFEC_SUB_RESTART_IMASK_RUN
		 * != 				-> sub_st	  == SFEC_SUB_NONE */
	else
		writel(FEC_ENET_MII, fep->hwp + FEC_IMASK);
		/* #PRI_OPENING 	-> sub_st	  == SFEC_SUB_RESTART_IMASK_MII
		 * != 				-> sub_st	  == SFEC_SUB_NONE */
	/* #PRI_OPENING 	-> primary_st == SFEC_PRI_POST_OPEN
	 * !=#PRI_OPENING 	-> primary_st == SFEC_PRI_RUNNING */
}

static void
fec_stop(struct net_device *ndev)
{
	/* primary_st == SFEC_PRI_RESTARTED/RUNNING | _STOPPED (drv_remove?)
	 * sub_st	  == SFEC_SUB_NONE */
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 rmii_mode = readl(fep->hwp + FEC_R_CNTRL) & (1 << 8);
	/* sub_st	  == SFEC_SUB_STOP_GET_RMII */

	/* We cannot expect a graceful transmit stop without link !!! */
	if (fep->link) {
		/* primary_st	  == SFEC_PRI_RUNNING */
		writel(1, fep->hwp + FEC_X_CNTRL); /* Graceful transmit stop */
		/* sub_st	  == SFEC_SUB_STOP_XMIT_STOP */
		udelay(10);
		if (!(readl(fep->hwp + FEC_IEVENT) & FEC_ENET_GRA))
			netdev_err(ndev, "Graceful transmit stop did not complete!\n");
		/* sub_st	  == SFEC_SUB_STOP_GET_IEVENT */
	}

	/* sub_st	  == SFEC_SUB_STOP_GET_RMII/GET_IEVENT */

	/* Whack a reset.  We should wait for this.
	 * For i.MX6SX SOC, enet use AXI bus, we use disable MAC
	 * instead of reset MAC itself.
	 */
	writel(1, fep->hwp + FEC_ECNTRL);
	/* sub_st	  == SFEC_SUB_STOP_RESET */
	udelay(10);
	writel(FEC_ENET_MII, fep->hwp + FEC_IMASK);
	/* sub_st	  == SFEC_SUB_STOP_MII_IMASK */
	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);
	/* sub_st	  == SFEC_SUB_STOP_MII_SPEED */

	/* We have to keep ENET enabled to have MII interrupt stay working */
	// note: this if is always TRUE
	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		writel(2, fep->hwp + FEC_ECNTRL);
		/* sub_st	  == SFEC_SUB_STOP_ECNTRL */
		writel(rmii_mode, fep->hwp + FEC_R_CNTRL);
		/* sub_st	  == SFEC_SUB_NONE */
	}
	/* primary_st == SFEC_PRI_STOPPED */
}

#if 0
// TODO: problem, bcs. clock code might be in NW?! (or late-initalized?!)
static void
fec_enet_hwtstamp(struct fec_enet_private *fep, unsigned ts,
	struct skb_shared_hwtstamps *hwtstamps)
{
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&fep->tmreg_lock, flags);
	ns = timecounter_cyc2time(&fep->tc, ts);
	spin_unlock_irqrestore(&fep->tmreg_lock, flags);

	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ns_to_ktime(ns);
}
#endif /* 0 */

static irqreturn_t
fec_enet_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = dev_id;
	struct fec_enet_private *fep = netdev_priv(ndev);
	irqreturn_t ret = IRQ_NONE;

	ret = IRQ_HANDLED;
//	if (int_events & FEC_ENET_MII) {
		complete(&fep->mdio_done);
//	}

#if 0 // TODO
	if (fep->ptp_clock)
		if (bstgw_fec_ptp_check_pps_event(fep))
			ret = ITRR_HANDLED;
#endif
	return ret;
}


/* ------------------------------------------------------------------------- */

/*
 * Phy section (TODO: unclear how to split at the moment)
 */
// TODO: PROBLEM where to handle; might even need duo-world handling;
static void fec_enet_adjust_link(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = ndev->phydev;
	int status_change = 0;

	/* Prevent a state halted on mii error */
	if (fep->mii_timeout && phy_dev->state == PHY_HALTED) {
		phy_dev->state = PHY_RESUMING;
		return;
	}

	/*
	 * If the netdev is down, or is going down, we're not interested
	 * in link state events, so just mark our idea of the link as down
	 * and ignore the event.
	 */
	//if (!netif_running(ndev) || !netif_device_present(ndev)) {
	//	fep->link = 0;
	/*} else*/ if (phy_dev->link) {
		if (!fep->link) {
			fep->link = phy_dev->link;
			status_change = 1;
		}

		if (fep->full_duplex != phy_dev->duplex) {
			fep->full_duplex = phy_dev->duplex;
			status_change = 1;
		}

		if (phy_dev->speed != fep->speed) {
			fep->speed = phy_dev->speed;
			status_change = 1;
		}

		/* if any of the above changed restart the FEC */
		if (status_change) {
//sw			napi_disable(&fep->napi);
//			netif_tx_lock_bh(ndev);
			/* primary_st == SFEC_PRI_RESTARTED/STOPPED/RUNNING
			 * sub_st	  == SFEC_SUB_NONE */
			fec_restart(ndev);
			/* primary_st == SFEC_PRI_RUNNING
			 * sub_st	  == SFEC_SUB_NONE */
//sw			netif_tx_wake_all_queues(ndev);
//			netif_tx_unlock_bh(ndev);
//sw			napi_enable(&fep->napi);
		}
	} else {
		if (fep->link) {
//sw			napi_disable(&fep->napi);
//			netif_tx_lock_bh(ndev);
			/* primary_st == SFEC_PRI_RUNNING
			 * sub_st	  == SFEC_SUB_NONE */
			fec_stop(ndev);
			/* primary_st == SFEC_PRI_STOPPED
			 * sub_st	  == SFEC_SUB_NONE */
//			netif_tx_unlock_bh(ndev);
//sw			napi_enable(&fep->napi);
			fep->link = phy_dev->link;
			status_change = 1;
		}
	}

	if (status_change)
		phy_print_status(phy_dev);
}

// TODO: what is that?
static void bangout(struct fec_enet_private *fep, unsigned val, int cnt)
{
	int bit_val;
	int prev_bit_val = 1;

	gpiod_direction_input(fep->gd_mdio);

	while (cnt) {
		cnt--;
		gpiod_direction_output(fep->gd_mdc, 0);
		bit_val = (val >> cnt) & 1;
		if (prev_bit_val != bit_val) {
			prev_bit_val = bit_val;
			if (bit_val)
				gpiod_direction_input(fep->gd_mdio);
			else
				gpiod_direction_output(fep->gd_mdio, 0);
		}
		gpiod_direction_input(fep->gd_mdc);
	}
}

static int phy_preamble(struct fec_enet_private *fep)
{
	bangout(fep, 0xffffffff, 32);
	if (gpiod_get_value(fep->gd_mdio) != 1) {
		bangout(fep, 0xffffffff, 32);
		if (gpiod_get_value(fep->gd_mdio) != 1) {
			dev_err(&fep->pdev->dev, "mdio is still low!\n");
			return -EIO;
		}
	}
	return 0;
}

static int fec_enet_mdio_read_bb(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fec_enet_private *fep = bus->priv;
	int val, ret;
	int i;

	val = FEC_MMFR_ST | FEC_MMFR_OP_READ |
			FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
			FEC_MMFR_TA;

	ret = phy_preamble(fep);
	if (ret)
		return ret;
	bangout(fep, val >> 17, 32 - 17);
	gpiod_direction_input(fep->gd_mdio);

	val = 0;
	for (i = 0; i < 17; i++) {
		val <<= 1;
		if (gpiod_get_value(fep->gd_mdio))
			val |= 1;
		gpiod_direction_output(fep->gd_mdc, 0);
		gpiod_direction_input(fep->gd_mdc);
	}

	dev_dbg(&fep->pdev->dev, "%s: phy: %02x reg:%02x val:%#x\n", __func__, mii_id,
		regnum, val);
	if (val & BIT(16)) {
		dev_err(&fep->pdev->dev, "phy not responding(%x)!!!\n", val);
		val = 0xffff; /* could return -EIO, but mii-tool looks for 0xffff */
	}
	return val;
}

static int fec_enet_mdio_write_bb(struct mii_bus *bus, int mii_id, int regnum,
		   u16 value)
{
	struct fec_enet_private *fep = bus->priv;
	int val;
	int ret;

	val = FEC_MMFR_ST | FEC_MMFR_OP_WRITE |
			FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
			FEC_MMFR_TA | FEC_MMFR_DATA(value);

	ret = phy_preamble(fep);
	if (ret)
		return ret;
	bangout(fep, val, 32);

	gpiod_direction_input(fep->gd_mdio);
	return 0;
}

static int fec_enet_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct fec_enet_private *fep = bus->priv;
	struct device *dev = &fep->pdev->dev;
	unsigned long time_left;
	uint int_events;
	int ret = 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		return ret;

	fep->mii_timeout = 0;
	reinit_completion(&fep->mdio_done);

	/* start a read op */
	writel(FEC_MMFR_ST | FEC_MMFR_OP_READ |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
		FEC_MMFR_TA, fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	time_left = wait_for_completion_timeout(&fep->mdio_done,
			usecs_to_jiffies(FEC_MII_TIMEOUT));
	if (time_left == 0) {
		fep->mii_timeout = 1;
		// TODO: add virtual access to distinguish from read access in fec_stop?
		int_events = readl(fep->hwp + FEC_IEVENT);
		if (!(int_events & FEC_ENET_MII)) {
			netdev_err(fep->netdev, "MDIO read timeout\n");
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	ret = FEC_MMFR_DATA(readl(fep->hwp + FEC_MII_DATA));

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int fec_enet_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	struct fec_enet_private *fep = bus->priv;
	struct device *dev = &fep->pdev->dev;
	unsigned long time_left;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		return ret;
	else
		ret = 0;

	fep->mii_timeout = 0;
	reinit_completion(&fep->mdio_done);

	/* start a write op */
	writel(FEC_MMFR_ST | FEC_MMFR_OP_WRITE |
		FEC_MMFR_PA(mii_id) | FEC_MMFR_RA(regnum) |
		FEC_MMFR_TA | FEC_MMFR_DATA(value),
		fep->hwp + FEC_MII_DATA);

	/* wait for end of transfer */
	time_left = wait_for_completion_timeout(&fep->mdio_done,
			usecs_to_jiffies(FEC_MII_TIMEOUT));
	if (time_left == 0) {
		fep->mii_timeout = 1;
		netdev_err(fep->netdev, "MDIO write timeout\n");
		ret  = -ETIMEDOUT;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

// TODO: clocks are also still a problem regarding setup/read/write
static int fec_enet_clk_enable(struct net_device *ndev, bool enable)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int ret;

	if (enable) {
		if (fep->clk_ptp) {
			mutex_lock(&fep->ptp_clk_mutex);
			ret = clk_prepare_enable(fep->clk_ptp);
			if (ret) {
				mutex_unlock(&fep->ptp_clk_mutex);
				goto failed_clk_ptp;
			} else {
				fep->ptp_clk_on = true;
			}
			mutex_unlock(&fep->ptp_clk_mutex);
		}

	} else {
		if (fep->clk_ptp) {
			mutex_lock(&fep->ptp_clk_mutex);
			clk_disable_unprepare(fep->clk_ptp);
			fep->ptp_clk_on = false;
			mutex_unlock(&fep->ptp_clk_mutex);
		}
	}

	return 0;

failed_clk_ptp:

	return ret;
}

static int fec_enet_mii_probe(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phy_dev = NULL;
	char mdio_bus_id[MII_BUS_ID_SIZE];
	char phy_name[MII_BUS_ID_SIZE + 3];
	int phy_id;
	int dev_id = fep->dev_id;

	if (fep->phy_node) {
		phy_dev = of_phy_connect(ndev, fep->phy_node,
					 &fec_enet_adjust_link, 0,
					 fep->phy_interface);
		if (!phy_dev) {
			netdev_err(ndev, "Unable to connect to phy\n");
			return -ENODEV;
		}
	} else {
		/* check for attached phy */
		for (phy_id = 0; (phy_id < PHY_MAX_ADDR); phy_id++) {
			if (!mdiobus_is_registered_device(fep->mii_bus, phy_id))
				continue;
			if (dev_id--)
				continue;
			strlcpy(mdio_bus_id, fep->mii_bus->id, MII_BUS_ID_SIZE);
			break;
		}

		if (phy_id >= PHY_MAX_ADDR) {
			netdev_info(ndev, "no PHY, assuming direct connection to switch\n");
			strlcpy(mdio_bus_id, "fixed-0", MII_BUS_ID_SIZE);
			phy_id = 0;
		}

		snprintf(phy_name, sizeof(phy_name),
			 PHY_ID_FMT, mdio_bus_id, phy_id);
		phy_dev = phy_connect(ndev, phy_name, &fec_enet_adjust_link,
				      fep->phy_interface);
	}

	if (IS_ERR(phy_dev)) {
		netdev_err(ndev, "could not attach to PHY\n");
		return PTR_ERR(phy_dev);
	}

	/* mask with MAC supported features */
	if ((fep->quirks & FEC_QUIRK_HAS_GBIT)
	    && !disable_giga) {
		phy_dev->supported &= PHY_GBIT_FEATURES;
		phy_dev->supported &= ~SUPPORTED_1000baseT_Half;
		phy_dev->supported |= SUPPORTED_Pause;
		phy_dev->advertising = phy_dev->supported;
	} else {
		phy_dev->advertising = phy_dev->supported & PHY_BASIC_FEATURES;
	}

	fep->link = 0;
	fep->full_duplex = 0;

	phy_attached_info(phy_dev);

	return 0;
}

static int fec_enet_mii_init(struct platform_device *pdev)
{
	static struct mii_bus *fec0_mii_bus;
	static int *fec_mii_bus_share;
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct device_node *node;
	int err = -ENXIO;
	u32 mii_speed, holdtime;

	/*
	 * The i.MX28 dual fec interfaces are not equal.
	 * Here are the differences:
	 *
	 *  - fec0 supports MII & RMII modes while fec1 only supports RMII
	 *  - fec0 acts as the 1588 time master while fec1 is slave
	 *  - external phys can only be configured by fec0
	 *
	 * That is to say fec1 can not work independently. It only works
	 * when fec0 is working. The reason behind this design is that the
	 * second interface is added primarily for Switch mode.
	 *
	 * Because of the last point above, both phys are attached on fec0
	 * mdio interface in board design, and need to be configured by
	 * fec0 mii_bus.
	 */
	if ((fep->quirks & FEC_QUIRK_SINGLE_MDIO) && fep->dev_id > 0) {
		/* fec1 uses fec0 mii_bus */
		if (mii_cnt && fec0_mii_bus) {
			fep->mii_bus = fec0_mii_bus;
			*fec_mii_bus_share = FEC0_MII_BUS_SHARE_TRUE;
			mii_cnt++;
			return 0;
		}
		return -ENOENT;
	}

	fep->mii_timeout = 0;

	/*
	 * Set MII speed to 2.5 MHz (= clk_get_rate() / 2 * phy_speed)
	 *
	 * The formula for FEC MDC is 'ref_freq / (MII_SPEED x 2)' while
	 * for ENET-MAC is 'ref_freq / ((MII_SPEED + 1) x 2)'.  The i.MX28
	 * Reference Manual has an error on this, and gets fixed on i.MX6Q
	 * document.
	 */
	mii_speed = DIV_ROUND_UP(clk_get_rate(fep->clk_ipg), 5000000);
	if (fep->quirks & FEC_QUIRK_ENET_MAC)
		mii_speed--;
	if (mii_speed > 63) {
		dev_err(&pdev->dev,
			"fec clock (%lu) too fast to get right mii speed\n",
			clk_get_rate(fep->clk_ipg));
		err = -EINVAL;
		goto err_out;
	}

	/*
	 * The i.MX28 and i.MX6 types have another filed in the MSCR (aka
	 * MII_SPEED) register that defines the MDIO output hold time. Earlier
	 * versions are RAZ there, so just ignore the difference and write the
	 * register always.
	 * The minimal hold time according to IEE802.3 (clause 22) is 10 ns.
	 * HOLDTIME + 1 is the number of clk cycles the fec is holding the
	 * output.
	 * The HOLDTIME bitfield takes values between 0 and 7 (inclusive).
	 * Given that ceil(clkrate / 5000000) <= 64, the calculation for
	 * holdtime cannot result in a value greater than 3.
	 */
	holdtime = DIV_ROUND_UP(clk_get_rate(fep->clk_ipg), 100000000) - 1;

	fep->phy_speed = mii_speed << 1 | holdtime << 8;

	writel(fep->phy_speed, fep->hwp + FEC_MII_SPEED);

	fep->mii_bus = mdiobus_alloc();
	if (fep->mii_bus == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	fep->mii_bus->name = "fec_enet_mii_bus";
	if (fep->gd_mdc && fep->gd_mdio) {
		fep->mii_bus->read = fec_enet_mdio_read_bb;
		fep->mii_bus->write = fec_enet_mdio_write_bb;
	} else {
		fep->mii_bus->read = fec_enet_mdio_read;
		fep->mii_bus->write = fec_enet_mdio_write;
	}
	snprintf(fep->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		pdev->name, fep->dev_id + 1);
	fep->mii_bus->priv = fep;
	fep->mii_bus->parent = &pdev->dev;

	node = of_get_child_by_name(pdev->dev.of_node, "mdio");
	if (node) {
		err = of_mdiobus_register(fep->mii_bus, node);
		of_node_put(node);
	} else if (fep->phy_node && !fep->fixed_link) {
		err = -EPROBE_DEFER;
	} else {
		err = mdiobus_register(fep->mii_bus);
	}

	if (err)
		goto err_out_free_mdiobus;

	mii_cnt++;

	/* save fec0 mii_bus */
	if (fep->quirks & FEC_QUIRK_SINGLE_MDIO) {
		fec0_mii_bus = fep->mii_bus;
		fec_mii_bus_share = &fep->mii_bus_share;
	}

	return 0;

err_out_free_mdiobus:
	mdiobus_free(fep->mii_bus);
err_out:
	return err;
}

static void fec_enet_mii_remove(struct fec_enet_private *fep)
{
	if (--mii_cnt == 0) {
		mdiobus_unregister(fep->mii_bus);
		mdiobus_free(fep->mii_bus);
	}
}

#if 0
static int fec_enet_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct phy_device *phydev = ndev->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	if (fep->bufdesc_ex) {
		if (cmd == SIOCSHWTSTAMP)
			return bstgw_fec_ptp_set(ndev, rq);
		if (cmd == SIOCGHWTSTAMP)
			return bstgw_fec_ptp_get(ndev, rq);
	}

	return phy_mii_ioctl(phydev, rq, cmd);
}
#endif

// TODO: NW, bcs. it causes pm_qos-request?
static inline bool fec_enet_irq_workaround(struct fec_enet_private *fep)
{
	/*
	 * Note: we have changed interrupts-extended to sp-interrupts without the
	 *       extended attribute format. The attribute 0 was gpr, not gpio,
	 * 		 i.e., below if condition is `true`.
	 */
//	if (intr_node && !strcmp(intr_node->name, "gpio")) {
	return true;
}

static int
fec_enet_open(struct net_device *ndev)
{
	/* primary_st == SFEC_PRI_PROBED
	 * sub_st	  == SFEC_SUB_NONE   */
	struct fec_enet_private *fep = netdev_priv(ndev);
	const struct platform_device_id *id_entry =
				platform_get_device_id(fep->pdev);
	int ret;

	ret = pm_runtime_get_sync(&fep->pdev->dev);
	if (ret < 0)
		return ret;

    /* TODO: should verify from SW */
	pinctrl_pm_select_default_state(&fep->pdev->dev);
	ret = fec_enet_clk_enable(ndev, true);
	if (ret)
		goto clk_enable;

	/* Init MAC prior to mii bus probe */
	fec_restart(ndev);
	/* primary_st == SFEC_PRI_POST_OPEN
	 * sub_st	  == SFEC_SUB_RESTART_RUN/MII */

	/* Probe and connect to PHY when open the interface */
	ret = fec_enet_mii_probe(ndev);
	if (ret)
		goto err_enet_mii_probe;

	if (fep->quirks & FEC_QUIRK_ERR006687)
		imx6q_cpuidle_fec_irqs_used();

//sw	napi_enable(&fep->napi);
	phy_start(ndev->phydev);
	// todo: add virtual sync point
	writel(47, fep->hwp + FEC_X_WMRK);
	/* primary_st == SFEC_PRI_RESTARTED/RUNNING
	 * sub_st	  == SFEC_SUB_NONE   */
//sw?:	netif_tx_start_all_queues(ndev);

// TODO: what does this even do? do we need it considering that
//		 we NOPed the pm ops?
	if ((id_entry->driver_data & FEC_QUIRK_BUG_WAITMODE) &&
	    !fec_enet_irq_workaround(fep))
		pm_qos_add_request(&fep->pm_qos_req,
				   PM_QOS_CPU_DMA_LATENCY,
				   0);
	else
		pm_qos_add_request(&fep->pm_qos_req,
				   PM_QOS_CPU_DMA_LATENCY,
				   PM_QOS_DEFAULT_VALUE);

	// TODO: kickable?
	device_set_wakeup_enable(&ndev->dev, false);

	return 0;

err_enet_mii_probe:
//sw	fec_enet_free_buffers(ndev);
//sw err_enet_alloc:
	fec_enet_clk_enable(ndev, false);
clk_enable:
	pm_runtime_mark_last_busy(&fep->pdev->dev);
	pm_runtime_put_autosuspend(&fep->pdev->dev);
	if (!fep->mii_bus_share)
		pinctrl_pm_select_sleep_state(&fep->pdev->dev);
	return ret;
}

// TODO: do we support close at all?
static int
fec_enet_close(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);

    netdev_info(ndev, "NW-FEC ENET CLOSE\n");

	phy_stop(ndev->phydev);

	// todo: add virtual sync. point for shutdown
	writel(48, fep->hwp + FEC_X_WMRK);

	//if (netif_device_present(ndev)) {
//sw		napi_disable(&fep->napi);
//sw		netif_tx_disable(ndev);
		fec_stop(ndev);
	//}

	phy_disconnect(ndev->phydev);
	ndev->phydev = NULL;

	if (fep->quirks & FEC_QUIRK_ERR006687)
		imx6q_cpuidle_fec_irqs_unused();

	fec_enet_clk_enable(ndev, false);
	pm_qos_remove_request(&fep->pm_qos_req);
	if (!fep->mii_bus_share)
		pinctrl_pm_select_sleep_state(&fep->pdev->dev);
	pm_runtime_mark_last_busy(&fep->pdev->dev);
	pm_runtime_put_autosuspend(&fep->pdev->dev);

//sw	fec_enet_free_buffers(ndev);

    netdev_info(ndev, "NW-FEC CLOSE done\n");
	return 0;
}

static const struct net_device_ops fec_netdev_ops = {
	.ndo_open		= NULL,
	.ndo_stop		= NULL,
	.ndo_start_xmit		= NULL,
	.ndo_select_queue       = NULL, // *
	.ndo_set_rx_mode	= NULL, // *
	.ndo_validate_addr	= NULL, // *
	.ndo_tx_timeout		= NULL,
	.ndo_set_mac_address	= NULL, // *
	.ndo_do_ioctl		= NULL,
	.ndo_set_features	= NULL, // *
};

 /*
  * XXX:  We need to clean up on failure exits here.
  *
  */
static int fec_enet_init(struct net_device *ndev)
{
	/* The FEC Ethernet specific entries in the device structure */
	ndev->watchdog_timeo = TX_TIMEOUT;
	ndev->netdev_ops = &fec_netdev_ops; // all NULL

	ndev->hw_features = ndev->features;

//	fec_restart(ndev); // will be called by fec_enet_open

	return 0;
}

//#ifdef CONFIG_OF // seems to be DT + OF; is enabled
static int fec_reset_phy(struct platform_device *pdev)
{
	int err, phy_reset;
	bool active_high = false;
	int msec = 1, phy_post_delay = 0;
	struct device_node *np = pdev->dev.of_node;

	if (!np)
		return 0;

	err = of_property_read_u32(np, "phy-reset-duration", &msec);
	/* A sane reset duration should not be longer than 1s */
	if (!err && msec > 1000)
		msec = 1;

	phy_reset = of_get_named_gpio(np, "phy-reset-gpios", 0);
	if (phy_reset == -EPROBE_DEFER)
		return phy_reset;
	else if (!gpio_is_valid(phy_reset))
		return 0;

	err = of_property_read_u32(np, "phy-reset-post-delay", &phy_post_delay);
	/* valid reset duration should be less than 1s */
	if (!err && phy_post_delay > 1000)
		return -EINVAL;

	active_high = of_property_read_bool(np, "phy-reset-active-high");

	err = devm_gpio_request_one(&pdev->dev, phy_reset,
			active_high ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
			"phy-reset");
	if (err) {
		dev_err(&pdev->dev, "failed to get phy-reset-gpios: %d\n", err);
		return err;
	}

	if (msec > 20)
		msleep(msec);
	else
		usleep_range(msec * 1000, msec * 1000 + 1000);

	gpio_set_value_cansleep(phy_reset, !active_high);

	if (!phy_post_delay)
		return 0;

	if (phy_post_delay > 20)
		msleep(phy_post_delay);
	else
		usleep_range(phy_post_delay * 1000,
			     phy_post_delay * 1000 + 1000);

	return 0;
}
//#else /* CONFIG_OF */
#if 0 // todo: useful? (prob. not?)
static int fec_reset_phy(struct platform_device *pdev)
{
	/*
	 * In case of platform probe, the reset has been done
	 * by machine code.
	 */
	return 0;
}
#endif
//#endif /* CONFIG_OF */

static void fec_enet_of_parse_stop_mode(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;
	struct fec_enet_private *fep = netdev_priv(dev);
	struct device_node *node;
	phandle phandle;
	u32 out_val[3];
	int ret;

	ret = of_property_read_u32_array(np, "stop-mode", out_val, 3);
	if (ret) {
		dev_dbg(&pdev->dev, "no stop-mode property\n");
		return;
	}

	phandle = *out_val;
	node = of_find_node_by_phandle(phandle);
	if (!node) {
		dev_dbg(&pdev->dev, "could not find gpr node by phandle\n");
		return;
	}

	fep->gpr.gpr = syscon_node_to_regmap(node);
	if (IS_ERR(fep->gpr.gpr)) {
		dev_dbg(&pdev->dev, "could not find gpr regmap\n");
		return;
	}

	of_node_put(node);

	fep->gpr.req_gpr = out_val[1];
	fep->gpr.req_bit = out_val[2];
}

static int
fec_probe(struct platform_device *pdev)
{
	struct fec_enet_private *fep;
	struct fec_platform_data *pdata;
	struct net_device *ndev;
	int i, irq, ret = 0;
	struct resource *r;
	const struct of_device_id *of_id;
	static int dev_id;
	struct device_node *np = pdev->dev.of_node, *phy_node;
	struct gpio_desc *gd;

	dev_err(&pdev->dev, "fec_probe called");

	/* Init network device */
	// TODO: we don't want queues!; but queue value of 0 is not allowed!
	ndev = alloc_etherdev(sizeof(struct fec_enet_private));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* setup board info structure */
	fep = netdev_priv(ndev);

	of_id = of_match_device(fec_dt_ids, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;
	fep->quirks = pdev->id_entry->driver_data;

	fep->netdev = ndev;

	/* default enable pause frame auto negotiation */
	if (fep->quirks & FEC_QUIRK_HAS_GBIT)
		fep->pause_flag |= FEC_PAUSE_FLAG_AUTONEG;

	/* Select default pin state */
    /* TODO: should verify from SW */
	pinctrl_pm_select_default_state(&pdev->dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fep->hwp = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(fep->hwp)) {
		ret = PTR_ERR(fep->hwp);
		goto failed_ioremap;
	}

	fep->pdev = pdev;
	fep->dev_id = dev_id++;

	platform_set_drvdata(pdev, ndev);

	if ((of_machine_is_compatible("fsl,imx6q") ||
	     of_machine_is_compatible("fsl,imx6dl")) &&
	    !of_property_read_bool(np, "fsl,err006687-workaround-present"))
		fep->quirks |= FEC_QUIRK_ERR006687;

	fec_enet_of_parse_stop_mode(pdev);

	gd = devm_gpiod_get_optional(&pdev->dev, "mdc", GPIOD_IN);
	if (IS_ERR(gd)) {
		if (PTR_ERR(gd) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get mdc gpio: %ld\n",
				PTR_ERR(gd));
		return PTR_ERR(gd);
	}
	fep->gd_mdc = gd;

	gd = devm_gpiod_get_optional(&pdev->dev, "mdio", GPIOD_IN);
	if (IS_ERR(gd)) {
		if (PTR_ERR(gd) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get mdio gpio: %ld\n",
				PTR_ERR(gd));
		return PTR_ERR(gd);
	}
	fep->gd_mdio = gd;

	phy_node = of_parse_phandle(np, "phy-handle", 0);
	if (!phy_node && of_phy_is_fixed_link(np)) {
		ret = of_phy_register_fixed_link(np);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"broken fixed-link specification\n");
			goto failed_phy;
		}
		phy_node = of_node_get(np);
		fep->fixed_link = true;
	}
	fep->phy_node = phy_node;

	ret = of_get_phy_mode(pdev->dev.of_node);
	if (ret < 0) {
		pdata = dev_get_platdata(&pdev->dev);
		if (pdata)
			fep->phy_interface = pdata->phy;
		else
			fep->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		fep->phy_interface = ret;
	}

	request_bus_freq(BUS_FREQ_HIGH);

	fep->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(fep->clk_ipg)) {
		ret = PTR_ERR(fep->clk_ipg);
		goto failed_clk;
	}

	fep->ptp_clk_on = false;
	mutex_init(&fep->ptp_clk_mutex);

	// TODO: disabled for the moment
	fep->bufdesc_ex = fep->quirks & FEC_QUIRK_HAS_BUFDESC_EX;
	//todo: fep->clk_ptp = devm_clk_get(&pdev->dev, "ptp");
	fep->clk_ptp = NULL;
	if (IS_ERR(fep->clk_ptp)) {
		fep->clk_ptp = NULL;
		fep->bufdesc_ex = false;
	}

	ret = fec_enet_clk_enable(ndev, true);
	if (ret)
		goto failed_clk;

	ret = clk_prepare_enable(fep->clk_ipg);
	if (ret)
		goto failed_clk_ipg;

	fep->reg_phy = devm_regulator_get(&pdev->dev, "phy");
	if (!IS_ERR(fep->reg_phy)) {
		ret = regulator_enable(fep->reg_phy);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to enable phy regulator: %d\n", ret);
			goto failed_regulator;
		}
	} else {
		if (PTR_ERR(fep->reg_phy) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto failed_regulator;
		}
		fep->reg_phy = NULL;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, FEC_MDIO_PM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = fec_reset_phy(pdev);
	if (ret)
		goto failed_reset;

	if (fep->bufdesc_ex)
		bstgw_fec_ptp_init(pdev);

	ret = fec_enet_init(ndev);
	if (ret)
		goto failed_init;

// Add irq handler for virtual IRQ for mdio_complete
	for (i = 0; i < FEC_IRQ_NUM; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			if (i)
				break;
			ret = irq;
			goto failed_irq;
		}
		ret = devm_request_irq(&pdev->dev, irq, fec_enet_interrupt,
				       0, pdev->name, ndev);
		if (ret)
			goto failed_irq;

		fep->irq[i] = irq;
	}

/*
	ret = of_property_read_u32(np, "fsl,wakeup_irq", &irq);
	if (!ret && irq < FEC_IRQ_NUM)
		fep->wake_irq = fep->irq[irq];
	else
		fep->wake_irq = fep->irq[0];
*/

	init_completion(&fep->mdio_done);

	/* board only enable one mii bus in default */
	if (!of_get_property(np, "fsl,mii-exclusive", NULL))
		fep->quirks |= FEC_QUIRK_SINGLE_MDIO;
	ret = fec_enet_mii_init(pdev);
	if (ret)
		goto failed_mii_init;

	/* Carrier starts down, phylib will bring it up */
	netif_carrier_off(ndev);
	fec_enet_clk_enable(ndev, false);
	pinctrl_pm_select_sleep_state(&pdev->dev);

	// TODO: we don't want that!
#if 0 //test
	ret = register_netdev(ndev);
	if (ret)
		goto failed_register;
#endif // test

	if (!fep->fixed_link) {
	}

	// TODO: kickable?
	device_init_wakeup(&ndev->dev, false);

	if (fep->bufdesc_ex && fep->ptp_clock)
		netdev_info(ndev, "registered PHC device %d\n", fep->dev_id);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	// TODO: is that callable from this context?
	fec_enet_open(ndev);
	//

	return 0;

failed_register:
	fec_enet_mii_remove(fep);
failed_mii_init:
failed_irq:
failed_init:
	bstgw_fec_ptp_stop(pdev);
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
failed_reset:
	pm_runtime_disable(&pdev->dev);
failed_regulator:
	clk_disable_unprepare(fep->clk_ipg);
failed_clk_ipg:
	fec_enet_clk_enable(ndev, false);
failed_clk:
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);
	of_node_put(phy_node);
failed_phy:
	dev_id--;
failed_ioremap:
	free_netdev(ndev);

	return ret;
}

// TODO: we actually don't want that at SW, but maybe we should still
//       keep it at NW
static int
fec_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct device_node *np = pdev->dev.of_node;

	dev_err(&pdev->dev, "fec_drv_remove called");

	// ok?
	fec_enet_close(ndev);
	//

	bstgw_fec_ptp_stop(pdev);
#if 0 // test
	unregister_netdev(ndev); //todo?
#endif // test
	fec_enet_mii_remove(fep);
	if (fep->reg_phy)
		regulator_disable(fep->reg_phy);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (of_phy_is_fixed_link(np))
		of_phy_deregister_fixed_link(np);
	of_node_put(fep->phy_node);
	free_netdev(ndev);

	return 0;
}


/* TODO: Do we need any of them for correct device operation?
	e.g., cf. sleep pinctrl mode on,
	and cf. quirk bug waitmode */

// TODO: kick completely if possible
static int __maybe_unused fec_suspend(struct device *dev)
{
	dev_err(dev, "fec_suspend\n");
	return 0;
}

static int __maybe_unused fec_resume(struct device *dev)
{
	dev_err(dev, "fec_resume\n");
	return 0;
}

static int __maybe_unused fec_runtime_suspend(struct device *dev)
{
	dev_err(dev, "fec_runtime_suspend\n");
	return 0;
}

static int __maybe_unused fec_runtime_resume(struct device *dev)
{
	dev_err(dev, "fec_runtime_resume\n");
	return 0;
}

static const struct dev_pm_ops fec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fec_suspend, fec_resume)
	SET_RUNTIME_PM_OPS(fec_runtime_suspend, fec_runtime_resume, NULL)
};

static struct platform_driver fec_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.pm	= &fec_pm_ops, // TODO: kick completely if possible
		.of_match_table = fec_dt_ids,
	},
	.id_table = fec_devtype,
	.probe	= fec_probe,
	.remove	= fec_drv_remove,
};

module_platform_driver(fec_driver);

MODULE_ALIAS("platform:"DRIVER_NAME);
MODULE_LICENSE("GPL");
