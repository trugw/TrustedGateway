/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	fec.h  --  Fast Ethernet Controller for Motorola ColdFire SoC
 *		   processors.
 *
 *	(C) Copyright 2000-2005, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000-2001, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef FEC_H
#define	FEC_H
/****************************************************************************/

#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/pm_qos.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>


#define FEC_IEVENT		0x004 /* Interrupt event reg */
#define FEC_IMASK		0x008 /* Interrupt mask reg */
#define FEC_R_DES_ACTIVE_0	0x010 /* Receive descriptor reg */
#define FEC_X_DES_ACTIVE_0	0x014 /* Transmit descriptor reg */
#define FEC_ECNTRL		0x024 /* Ethernet control reg */
#define FEC_MII_DATA		0x040 /* MII manage frame reg */
#define FEC_MII_SPEED		0x044 /* MII speed control reg */
#define FEC_MIB_CTRLSTAT	0x064 /* MIB control/status reg */
#define FEC_R_CNTRL		0x084 /* Receive control reg */
#define FEC_X_CNTRL		0x0c4 /* Transmit Control reg */
#define FEC_ADDR_LOW		0x0e4 /* Low 32bits MAC address */
#define FEC_ADDR_HIGH		0x0e8 /* High 16bits MAC address */
#define FEC_OPD			0x0ec /* Opcode + Pause duration */
#define FEC_TXIC0		0x0f0 /* Tx Interrupt Coalescing for ring 0 */
#define FEC_TXIC1		0x0f4 /* Tx Interrupt Coalescing for ring 1 */
#define FEC_TXIC2		0x0f8 /* Tx Interrupt Coalescing for ring 2 */
#define FEC_RXIC0		0x100 /* Rx Interrupt Coalescing for ring 0 */
#define FEC_RXIC1		0x104 /* Rx Interrupt Coalescing for ring 1 */
#define FEC_RXIC2		0x108 /* Rx Interrupt Coalescing for ring 2 */
#define FEC_HASH_TABLE_HIGH	0x118 /* High 32bits hash table */
#define FEC_HASH_TABLE_LOW	0x11c /* Low 32bits hash table */
#define FEC_GRP_HASH_TABLE_HIGH	0x120 /* High 32bits hash table */
#define FEC_GRP_HASH_TABLE_LOW	0x124 /* Low 32bits hash table */
#define FEC_X_WMRK		0x144 /* FIFO transmit water mark */
#define FEC_R_BOUND		0x14c /* FIFO receive bound reg */
#define FEC_R_FSTART		0x150 /* FIFO receive start reg */
#define FEC_R_DES_START_1	0x160 /* Receive descriptor ring 1 */
#define FEC_X_DES_START_1	0x164 /* Transmit descriptor ring 1 */
#define FEC_R_BUFF_SIZE_1	0x168 /* Maximum receive buff ring1 size */
#define FEC_R_DES_START_2	0x16c /* Receive descriptor ring 2 */
#define FEC_X_DES_START_2	0x170 /* Transmit descriptor ring 2 */
#define FEC_R_BUFF_SIZE_2	0x174 /* Maximum receive buff ring2 size */
#define FEC_R_DES_START_0	0x180 /* Receive descriptor ring */
#define FEC_X_DES_START_0	0x184 /* Transmit descriptor ring */
#define FEC_R_BUFF_SIZE_0	0x188 /* Maximum receive buff size */
#define FEC_R_FIFO_RSFL		0x190 /* Receive FIFO section full threshold */
#define FEC_R_FIFO_RSEM		0x194 /* Receive FIFO section empty threshold */
#define FEC_R_FIFO_RAEM		0x198 /* Receive FIFO almost empty threshold */
#define FEC_R_FIFO_RAFL		0x19c /* Receive FIFO almost full threshold */
#define FEC_FTRL		0x1b0 /* Frame truncation receive length*/
#define FEC_RACC		0x1c4 /* Receive Accelerator function */
#define FEC_RCMR_1		0x1c8 /* Receive classification match ring 1 */
#define FEC_RCMR_2		0x1cc /* Receive classification match ring 2 */
#define FEC_DMA_CFG_1		0x1d8 /* DMA class configuration for ring 1 */
#define FEC_DMA_CFG_2		0x1dc /* DMA class Configuration for ring 2 */
#define FEC_R_DES_ACTIVE_1	0x1e0 /* Rx descriptor active for ring 1 */
#define FEC_X_DES_ACTIVE_1	0x1e4 /* Tx descriptor active for ring 1 */
#define FEC_R_DES_ACTIVE_2	0x1e8 /* Rx descriptor active for ring 2 */
#define FEC_X_DES_ACTIVE_2	0x1ec /* Tx descriptor active for ring 2 */
#define FEC_QOS_SCHEME		0x1f0 /* Set multi queues Qos scheme */
#define FEC_LPI_SLEEP		0x1f4 /* Set IEEE802.3az LPI Sleep Ts time */
#define FEC_LPI_WAKE		0x1f8 /* Set IEEE802.3az LPI Wake Tw time */
#define FEC_MIIGSK_CFGR		0x300 /* MIIGSK Configuration reg */
#define FEC_MIIGSK_ENR		0x308 /* MIIGSK Enable reg */

#define BM_MIIGSK_CFGR_MII		0x00
#define BM_MIIGSK_CFGR_RMII		0x01
#define BM_MIIGSK_CFGR_FRCONT_10M	0x40

#define RMON_T_DROP		0x200 /* Count of frames not cntd correctly */
#define RMON_T_PACKETS		0x204 /* RMON TX packet count */
#define RMON_T_BC_PKT		0x208 /* RMON TX broadcast pkts */
#define RMON_T_MC_PKT		0x20c /* RMON TX multicast pkts */
#define RMON_T_CRC_ALIGN	0x210 /* RMON TX pkts with CRC align err */
#define RMON_T_UNDERSIZE	0x214 /* RMON TX pkts < 64 bytes, good CRC */
#define RMON_T_OVERSIZE		0x218 /* RMON TX pkts > MAX_FL bytes good CRC */
#define RMON_T_FRAG		0x21c /* RMON TX pkts < 64 bytes, bad CRC */
#define RMON_T_JAB		0x220 /* RMON TX pkts > MAX_FL bytes, bad CRC */
#define RMON_T_COL		0x224 /* RMON TX collision count */
#define RMON_T_P64		0x228 /* RMON TX 64 byte pkts */
#define RMON_T_P65TO127		0x22c /* RMON TX 65 to 127 byte pkts */
#define RMON_T_P128TO255	0x230 /* RMON TX 128 to 255 byte pkts */
#define RMON_T_P256TO511	0x234 /* RMON TX 256 to 511 byte pkts */
#define RMON_T_P512TO1023	0x238 /* RMON TX 512 to 1023 byte pkts */
#define RMON_T_P1024TO2047	0x23c /* RMON TX 1024 to 2047 byte pkts */
#define RMON_T_P_GTE2048	0x240 /* RMON TX pkts > 2048 bytes */
#define RMON_T_OCTETS		0x244 /* RMON TX octets */
#define IEEE_T_DROP		0x248 /* Count of frames not counted crtly */
#define IEEE_T_FRAME_OK		0x24c /* Frames tx'd OK */
#define IEEE_T_1COL		0x250 /* Frames tx'd with single collision */
#define IEEE_T_MCOL		0x254 /* Frames tx'd with multiple collision */
#define IEEE_T_DEF		0x258 /* Frames tx'd after deferral delay */
#define IEEE_T_LCOL		0x25c /* Frames tx'd with late collision */
#define IEEE_T_EXCOL		0x260 /* Frames tx'd with excesv collisions */
#define IEEE_T_MACERR		0x264 /* Frames tx'd with TX FIFO underrun */
#define IEEE_T_CSERR		0x268 /* Frames tx'd with carrier sense err */
#define IEEE_T_SQE		0x26c /* Frames tx'd with SQE err */
#define IEEE_T_FDXFC		0x270 /* Flow control pause frames tx'd */
#define IEEE_T_OCTETS_OK	0x274 /* Octet count for frames tx'd w/o err */
#define RMON_R_PACKETS		0x284 /* RMON RX packet count */
#define RMON_R_BC_PKT		0x288 /* RMON RX broadcast pkts */
#define RMON_R_MC_PKT		0x28c /* RMON RX multicast pkts */
#define RMON_R_CRC_ALIGN	0x290 /* RMON RX pkts with CRC alignment err */
#define RMON_R_UNDERSIZE	0x294 /* RMON RX pkts < 64 bytes, good CRC */
#define RMON_R_OVERSIZE		0x298 /* RMON RX pkts > MAX_FL bytes good CRC */
#define RMON_R_FRAG		0x29c /* RMON RX pkts < 64 bytes, bad CRC */
#define RMON_R_JAB		0x2a0 /* RMON RX pkts > MAX_FL bytes, bad CRC */
#define RMON_R_RESVD_O		0x2a4 /* Reserved */
#define RMON_R_P64		0x2a8 /* RMON RX 64 byte pkts */
#define RMON_R_P65TO127		0x2ac /* RMON RX 65 to 127 byte pkts */
#define RMON_R_P128TO255	0x2b0 /* RMON RX 128 to 255 byte pkts */
#define RMON_R_P256TO511	0x2b4 /* RMON RX 256 to 511 byte pkts */
#define RMON_R_P512TO1023	0x2b8 /* RMON RX 512 to 1023 byte pkts */
#define RMON_R_P1024TO2047	0x2bc /* RMON RX 1024 to 2047 byte pkts */
#define RMON_R_P_GTE2048	0x2c0 /* RMON RX pkts > 2048 bytes */
#define RMON_R_OCTETS		0x2c4 /* RMON RX octets */
#define IEEE_R_DROP		0x2c8 /* Count frames not counted correctly */
#define IEEE_R_FRAME_OK		0x2cc /* Frames rx'd OK */
#define IEEE_R_CRC		0x2d0 /* Frames rx'd with CRC err */
#define IEEE_R_ALIGN		0x2d4 /* Frames rx'd with alignment err */
#define IEEE_R_MACERR		0x2d8 /* Receive FIFO overflow count */
#define IEEE_R_FDXFC		0x2dc /* Flow control pause frames rx'd */
#define IEEE_R_OCTETS_OK	0x2e0 /* Octet cnt for frames rx'd w/o err */


/*
 *	Define the buffer descriptor structure.
 *
 *	Evidently, ARM SoCs have the FEC block generated in a
 *	little endian mode so adjust endianness accordingly.
 */
#define fec32_to_cpu le32_to_cpu
#define fec16_to_cpu le16_to_cpu
#define cpu_to_fec32 cpu_to_le32
#define cpu_to_fec16 cpu_to_le16
#define __fec32 __le32
#define __fec16 __le16

struct bufdesc {
	__fec16 cbd_datlen;	/* Data length */
	__fec16 cbd_sc;		/* Control and status info */
	__fec32 cbd_bufaddr;	/* Buffer address */
};

struct bufdesc_ex {
	struct bufdesc desc;
	__fec32 cbd_esc;
	__fec32 cbd_prot;
	__fec32 cbd_bdu;
	__fec32 ts;
	__fec16 res0[4];
};

/*
 *	The following definitions courtesy of commproc.h, which where
 *	Copyright (c) 1997 Dan Malek (dmalek@jlc.net).
 */
#define BD_SC_EMPTY	((ushort)0x8000)	/* Receive is empty */
#define BD_SC_READY	((ushort)0x8000)	/* Transmit is ready */
#define BD_SC_WRAP	((ushort)0x2000)	/* Last buffer descriptor */
#define BD_SC_INTRPT	((ushort)0x1000)	/* Interrupt on change */
#define BD_SC_CM	((ushort)0x0200)	/* Continuous mode */
#define BD_SC_ID	((ushort)0x0100)	/* Rec'd too many idles */
#define BD_SC_P		((ushort)0x0100)	/* xmt preamble */
#define BD_SC_BR	((ushort)0x0020)	/* Break received */
#define BD_SC_FR	((ushort)0x0010)	/* Framing error */
#define BD_SC_PR	((ushort)0x0008)	/* Parity error */
#define BD_SC_OV	((ushort)0x0002)	/* Overrun */
#define BD_SC_CD	((ushort)0x0001)	/* ?? */

/* Buffer descriptor control/status used by Ethernet receive.
 */
#define BD_ENET_RX_EMPTY	((ushort)0x8000)
#define BD_ENET_RX_WRAP		((ushort)0x2000)
#define BD_ENET_RX_INTR		((ushort)0x1000)
#define BD_ENET_RX_LAST		((ushort)0x0800)
#define BD_ENET_RX_FIRST	((ushort)0x0400)
#define BD_ENET_RX_MISS		((ushort)0x0100)
#define BD_ENET_RX_LG		((ushort)0x0020)
#define BD_ENET_RX_NO		((ushort)0x0010)
#define BD_ENET_RX_SH		((ushort)0x0008)
#define BD_ENET_RX_CR		((ushort)0x0004)
#define BD_ENET_RX_OV		((ushort)0x0002)
#define BD_ENET_RX_CL		((ushort)0x0001)
#define BD_ENET_RX_STATS	((ushort)0x013f)	/* All status bits */

/* Enhanced buffer descriptor control/status used by Ethernet receive */
#define BD_ENET_RX_VLAN		0x00000004

/* Buffer descriptor control/status used by Ethernet transmit.
 */
#define BD_ENET_TX_READY	((ushort)0x8000)
#define BD_ENET_TX_PAD		((ushort)0x4000)
#define BD_ENET_TX_WRAP		((ushort)0x2000)
#define BD_ENET_TX_INTR		((ushort)0x1000)
#define BD_ENET_TX_LAST		((ushort)0x0800)
#define BD_ENET_TX_TC		((ushort)0x0400)
#define BD_ENET_TX_DEF		((ushort)0x0200)
#define BD_ENET_TX_HB		((ushort)0x0100)
#define BD_ENET_TX_LC		((ushort)0x0080)
#define BD_ENET_TX_RL		((ushort)0x0040)
#define BD_ENET_TX_RCMASK	((ushort)0x003c)
#define BD_ENET_TX_UN		((ushort)0x0002)
#define BD_ENET_TX_CSL		((ushort)0x0001)
#define BD_ENET_TX_STATS	((ushort)0x0fff)	/* All status bits */

/* enhanced buffer descriptor control/status used by Ethernet transmit */
#define BD_ENET_TX_INT		0x40000000
#define BD_ENET_TX_TS		0x20000000
#define BD_ENET_TX_PINS		0x10000000
#define BD_ENET_TX_IINS		0x08000000


/* This device has up to three irqs on some platforms */
#define FEC_IRQ_NUM		4

/* Maximum number of queues supported
 * ENET with AVB IP can support up to 3 independent tx queues and rx queues.
 * User can point the queue number that is less than or equal to 3.
 */
#define FEC_ENET_MAX_TX_QS	3
#define FEC_ENET_MAX_RX_QS	3

#define FEC_R_DES_START(X)	(((X) == 1) ? FEC_R_DES_START_1 : \
				(((X) == 2) ? \
					FEC_R_DES_START_2 : FEC_R_DES_START_0))
#define FEC_X_DES_START(X)	(((X) == 1) ? FEC_X_DES_START_1 : \
				(((X) == 2) ? \
					FEC_X_DES_START_2 : FEC_X_DES_START_0))
#define FEC_R_BUFF_SIZE(X)	(((X) == 1) ? FEC_R_BUFF_SIZE_1 : \
				(((X) == 2) ? \
					FEC_R_BUFF_SIZE_2 : FEC_R_BUFF_SIZE_0))

#define FEC_DMA_CFG(X)		(((X) == 2) ? FEC_DMA_CFG_2 : FEC_DMA_CFG_1)

#define DMA_CLASS_EN		(1 << 16)
#define FEC_RCMR(X)		(((X) == 2) ? FEC_RCMR_2 : FEC_RCMR_1)
#define IDLE_SLOPE_MASK		0xffff
#define IDLE_SLOPE_1		0x200 /* BW fraction: 0.5 */
#define IDLE_SLOPE_2		0x200 /* BW fraction: 0.5 */
#define IDLE_SLOPE(X)		(((X) == 1) ?				\
				(IDLE_SLOPE_1 & IDLE_SLOPE_MASK) :	\
				(IDLE_SLOPE_2 & IDLE_SLOPE_MASK))
#define RCMR_MATCHEN		(0x1 << 16)
#define RCMR_CMP_CFG(v, n)	(((v) & 0x7) <<  (n << 2))
#define RCMR_CMP_1		(RCMR_CMP_CFG(0, 0) | RCMR_CMP_CFG(1, 1) | \
				RCMR_CMP_CFG(2, 2) | RCMR_CMP_CFG(3, 3))
#define RCMR_CMP_2		(RCMR_CMP_CFG(4, 0) | RCMR_CMP_CFG(5, 1) | \
				RCMR_CMP_CFG(6, 2) | RCMR_CMP_CFG(7, 3))
#define RCMR_CMP(X)		(((X) == 1) ? RCMR_CMP_1 : RCMR_CMP_2)
#define FEC_TX_BD_FTYPE(X)	(((X) & 0xf) << 20)

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it it best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */

#define FEC_ENET_RX_PAGES	256
#define FEC_ENET_RX_FRSIZE	2048
#define FEC_ENET_RX_FRPPG	(PAGE_SIZE / FEC_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(FEC_ENET_RX_FRPPG * FEC_ENET_RX_PAGES)
#define FEC_ENET_TX_FRSIZE	2048
#define FEC_ENET_TX_FRPPG	(PAGE_SIZE / FEC_ENET_TX_FRSIZE)
#define TX_RING_SIZE		512	/* Must be power of two */
#define TX_RING_MOD_MASK	511	/*   for this to work */

#define BD_ENET_RX_INT		0x00800000
#define BD_ENET_RX_PTP		((ushort)0x0400)
#define BD_ENET_RX_ICE		0x00000020
#define BD_ENET_RX_PCR		0x00000010
#define FLAG_RX_CSUM_ENABLED	(BD_ENET_RX_ICE | BD_ENET_RX_PCR)
#define FLAG_RX_CSUM_ERROR	(BD_ENET_RX_ICE | BD_ENET_RX_PCR)

#define FEC0_MII_BUS_SHARE_TRUE	1

/* Interrupt events/masks. */
#define FEC_ENET_HBERR  ((uint)0x80000000)      /* Heartbeat error */
#define FEC_ENET_BABR   ((uint)0x40000000)      /* Babbling receiver */
#define FEC_ENET_BABT   ((uint)0x20000000)      /* Babbling transmitter */
#define FEC_ENET_GRA    ((uint)0x10000000)      /* Graceful stop complete */
#define FEC_ENET_TXF_0	((uint)0x08000000)	/* Full frame transmitted */
#define FEC_ENET_TXF_1	((uint)0x00000008)	/* Full frame transmitted */
#define FEC_ENET_TXF_2	((uint)0x00000080)	/* Full frame transmitted */
#define FEC_ENET_TXB    ((uint)0x04000000)      /* A buffer was transmitted */
#define FEC_ENET_RXF_0	((uint)0x02000000)	/* Full frame received */
#define FEC_ENET_RXF_1	((uint)0x00000002)	/* Full frame received */
#define FEC_ENET_RXF_2	((uint)0x00000020)	/* Full frame received */
#define FEC_ENET_RXB    ((uint)0x01000000)      /* A buffer was received */
#define FEC_ENET_MII    ((uint)0x00800000)      /* MII interrupt */
#define FEC_ENET_EBERR  ((uint)0x00400000)      /* SDMA bus error */
#define FEC_ENET_WAKEUP	((uint)0x00020000)	/* Wakeup request */
#define FEC_ENET_TXF	(FEC_ENET_TXF_0 | FEC_ENET_TXF_1 | FEC_ENET_TXF_2)
#define FEC_ENET_RXF	(FEC_ENET_RXF_0 | FEC_ENET_RXF_1 | FEC_ENET_RXF_2)
#define FEC_ENET_TS_AVAIL       ((uint)0x00010000)
#define FEC_ENET_TS_TIMER       ((uint)0x00008000)

#define FEC_DEFAULT_IMASK (FEC_ENET_TXF | FEC_ENET_RXF | FEC_ENET_MII)
#define FEC_NAPI_IMASK	FEC_ENET_MII
#define FEC_RX_DISABLED_IMASK (FEC_DEFAULT_IMASK & (~FEC_ENET_RXF))

#define FEC_ENET_ETHEREN	((uint)0x00000002)
#define FEC_ENET_TXC_DLY	((uint)0x00010000)
#define FEC_ENET_RXC_DLY	((uint)0x00020000)

/* ENET interrupt coalescing macro define */
#define FEC_ITR_CLK_SEL		(0x1 << 30)
#define FEC_ITR_EN		(0x1 << 31)
#define FEC_ITR_ICFT(X)		(((X) & 0xff) << 20)
#define FEC_ITR_ICTT(X)		((X) & 0xffff)
#define FEC_ITR_ICFT_DEFAULT	200  /* Set 200 frame count threshold */
#define FEC_ITR_ICTT_DEFAULT	1000 /* Set 1000us timer threshold */

#define FEC_VLAN_TAG_LEN	0x04
#define FEC_ETHTYPE_LEN		0x02

/* Controller is ENET-MAC */
#define FEC_QUIRK_ENET_MAC		(1 << 0)
/* Controller has GBIT support */
#define FEC_QUIRK_HAS_GBIT		(1 << 3)
/* Controller has extend desc buffer */
#define FEC_QUIRK_HAS_BUFDESC_EX	(1 << 4)
/* Controller has hardware checksum support */
#define FEC_QUIRK_HAS_CSUM		(1 << 5)
/* Controller has hardware vlan support */
#define FEC_QUIRK_HAS_VLAN		(1 << 6)
/* ENET IP errata ERR006358
 *
 * If the ready bit in the transmit buffer descriptor (TxBD[R]) is previously
 * detected as not set during a prior frame transmission, then the
 * ENET_TDAR[TDAR] bit is cleared at a later time, even if additional TxBDs
 * were added to the ring and the ENET_TDAR[TDAR] bit is set. This results in
 * frames not being transmitted until there is a 0-to-1 transition on
 * ENET_TDAR[TDAR].
 */
#define FEC_QUIRK_ERR006358		(1 << 7)
/* Controller has only one MDIO bus */
#define FEC_QUIRK_SINGLE_MDIO		(1 << 11)
/* Controller supports RACC register */
#define FEC_QUIRK_HAS_RACC		(1 << 12)
/* Interrupt doesn't wake CPU from deep idle */
#define FEC_QUIRK_ERR006687		(1 << 14)
/*
 * i.MX6Q/DL ENET cannot wake up system in wait mode because ENET tx & rx
 * interrupt signal don't connect to GPC. So use pm qos to avoid cpu enter
 * to wait mode.
 */
#define FEC_QUIRK_BUG_WAITMODE		(1 << 16)

struct bufdesc_prop {
	int qid;
	/* Address of Rx and Tx buffers */
	struct bufdesc	*base;
	struct bufdesc	*last;
	struct bufdesc	*cur;
	void __iomem	*reg_desc_active;
	dma_addr_t	dma;
	unsigned short ring_size;
	unsigned char dsize;
	unsigned char dsize_log2;
};

struct fec_enet_stop_mode {
	struct regmap *gpr;
	u8 req_gpr;
	u8 req_bit;
};

/* bd.cur points to the currently available buffer.
 * pending_tx tracks the current buffer that is being sent by the
 * controller.  When bd.cur and pending_tx are equal, nothing is pending.
 */
struct fec_enet_priv_tx_q {
	struct bufdesc_prop bd;
	unsigned char *tx_bounce[TX_RING_SIZE];
	struct  sk_buff *tx_skbuff[TX_RING_SIZE];

	unsigned short tx_stop_threshold;
	unsigned short tx_wake_threshold;

	struct bufdesc	*pending_tx;
	char *tso_hdrs;
	dma_addr_t tso_hdrs_dma;
};

struct fec_enet_priv_rx_q {
	struct bufdesc_prop bd;
	struct  sk_buff *rx_skbuff[RX_RING_SIZE];
};

/* The FEC buffer descriptors track the ring buffers.
 */
struct fec_enet_private {
	/* Hardware registers of the FEC device */
	void __iomem *hwp;

	struct net_device *netdev;

	struct clk *clk_ipg;
	struct clk *clk_ptp;

	bool ptp_clk_on;
	struct mutex ptp_clk_mutex;

	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	uint	events;

	struct	platform_device *pdev;

	int	dev_id;

	/* Phylib and MDIO interface */
	struct	mii_bus *mii_bus;
	int	mii_timeout;
	int	mii_bus_share;
	uint	phy_speed;
	phy_interface_t	phy_interface;
	struct device_node *phy_node;
	int	link;
	bool	fixed_link;
	int	full_duplex;
	int	speed;
	struct	completion mdio_done;
	int	irq[FEC_IRQ_NUM];
	bool	bufdesc_ex;
	int	pause_flag;
//	int	wake_irq;
	u32	quirks;

	struct	napi_struct napi;
	int	csum_flags;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	unsigned long last_overflow_check;
	spinlock_t tmreg_lock;
	struct cyclecounter cc;
	struct timecounter tc;
	int rx_hwtstamp_filter;
	u32 base_incval;
	u32 cycle_speed;
	int hwts_rx_en;
	int hwts_tx_en;
	struct delayed_work time_keep;
	struct regulator *reg_phy;
	struct pm_qos_request pm_qos_req;

	/* ptp clock period in ns*/
	unsigned int ptp_inc;

	/* pps  */
	int pps_channel;
	unsigned int reload_period;
	int pps_enable;
	unsigned int next_counter;

	struct fec_enet_stop_mode gpr;
	struct gpio_desc *gd_mdc;
	struct gpio_desc *gd_mdio;
};

void bstgw_fec_ptp_init(struct platform_device *pdev);
void bstgw_fec_ptp_stop(struct platform_device *pdev);
void bstgw_fec_ptp_start_cyclecounter(struct net_device *ndev);
int bstgw_fec_ptp_set(struct net_device *ndev, struct ifreq *ifr);
int bstgw_fec_ptp_get(struct net_device *ndev, struct ifreq *ifr);
uint bstgw_fec_ptp_check_pps_event(struct fec_enet_private *fep);

/****************************************************************************/
#endif /* FEC_H */
