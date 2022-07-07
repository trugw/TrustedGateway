/*
 * Fast Ethernet Controller (FEC) driver for Motorola MPC8xx.
 * Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
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
#include <drivers/bstgw/imx_fec.h>

#include <drivers/imx_csu.h>
#include <drivers/dt.h>
#include <errno.h>
#include <io.h>
#include <kernel/dt.h>
#include <kernel/panic.h>
#include <malloc.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <secloak/emulation.h>
#include <stdlib.h>
#include <util.h>

#include <dpdk/rte_ether.h>
#include <trace.h>

#include <newlib/endian.h> // ntohs()
#include <newlib/linux/sys/socket.h> // sockaddr

#include <errno.h> // EADDRNOTAVAIL

#include <kernel/tee_time.h>

#include <tee/cache.h>

#include <bstgw/dma_pool.h>

#include <bstgw_netif_utils.h>

#include <drivers/bstgw/nw_mmio_traps.h> // fec_emu_check()

//#define BSTGW_FEC_IMX_DEBUG

#ifdef BSTGW_DBG_STATS
unsigned long int full_bd_ring = 0;
unsigned long int max_tx_complete = 0;
unsigned long int hit_err006358 = 0;
unsigned long int fec_itr_calls = 0;
unsigned long int pktq_slock_fail = 0, in_queue_slock_fail = 0;
unsigned long int xmit_napi_leaves = 0, rx_napi_leaves = 0;
#endif

// from Linux
static inline void prefetch(const void *va_ptr) {
	asm __volatile__ ( "pld\t%a0" :: "p" (va_ptr) );
}

/**************************************************************/

static void set_multicast_list(struct net_device *ndev);

// moved to header
/* Pause frame feild and FIFO threshold */
//#define FEC_ENET_FCE	(1 << 5)

/*#define FEC_ENET_RSEM_V	0x84
#define FEC_ENET_RSFL_V	16
#define FEC_ENET_RAEM_V	0x8
#define FEC_ENET_RAFL_V	0x8
#define FEC_ENET_OPD_V	0xFFF0*/

struct platform_device_id {
    char name[20];
    unsigned long driver_data;
};

enum imx_fec_type {
	BSTGW_IMX6Q_FEC = 0,
};

static struct platform_device_id fec_devtype[] = {
	{
		.name = "imx6q-fec",
		// TODO: disable bufdesc_ex for the moment to get rid of
		//		 the PTP clock; we might have to re-enable it later
		//		 when we want to add VLAN/CSUM support which depend
		// 		 on bufdesc_ex
		.driver_data = FEC_QUIRK_ENET_MAC | FEC_QUIRK_HAS_GBIT |
				/*FEC_QUIRK_HAS_BUFDESC_EX |*/ FEC_QUIRK_HAS_CSUM |
				FEC_QUIRK_HAS_VLAN | FEC_QUIRK_ERR006358 |
				FEC_QUIRK_HAS_RACC | FEC_QUIRK_BUG_WAITMODE,
	}, {
		/* sentinel */
	}
};

//static unsigned char macaddr[RTE_ETHER_ADDR_LEN];
static struct rte_ether_addr macaddr;

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
//#define FEC_MMFR_DATA(v)	(v & 0xffff) // moved to header
/* FEC ECR bits definition */
#define FEC_ECR_MAGICEN		(1 << 2)
#define FEC_ECR_SLEEP		(1 << 3)


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
// TODO: add the (100 * 2 +) again? not sure if otherwise too few skb descs
#define FEC_MAX_SKB_DESCS	(/*FEC_MAX_TSO_SEGS * 2 +*/ MAX_SKB_FRAGS)

static struct bufdesc *fec_enet_get_nextdesc(struct bufdesc *bdp,
					     struct bufdesc_prop *bd)
{
	return (bdp >= bd->last) ? bd->base
			: (struct bufdesc *)(((void *)bdp) + bd->dsize);
}

static int fec_enet_get_bd_index(struct bufdesc *bdp,
				 struct bufdesc_prop *bd)
{
	return ((const char *)bdp - (const char *)bd->base) >> bd->dsize_log2;
}

static int fec_enet_get_free_txdesc_num(struct fec_enet_priv_tx_q *txq)
{
	int entries;

	entries = (((const char *)txq->pending_tx -
			(const char *)txq->bd.cur) >> txq->bd.dsize_log2) - 1;

	return entries >= 0 ? entries : entries + txq->bd.ring_size;
}


static int fec_trigger_tx(struct fec_enet_priv_tx_q *txq, __fec16 cbd_sc,
			  struct bufdesc *first_bdp, struct bufdesc *next_bdp,
			  struct sk_buff *skb, unsigned index)
{
	/* Save skb pointer */
	txq->tx_skbuff[index] = skb;
//TODO:	skb_tx_timestamp(skb); // TODO: this would touch the buffer again!?
	/* Make sure the updates to rest of the descriptor are performed before
	 * transferring ownership.
	 */
	asm volatile ("dmb oshst"); // dma_wmb();
	/* Send it on its way.  Transfer ownership
	 * Keep window for interrupt between the next 3 lines as small as
	 * possible, to avoid "tx int lost" in case no more tx interrupts
	 * happen within 2 seconds.
	 */
	first_bdp->cbd_sc = cbd_sc;	/* cpu_to_fec16 already done */
	//TODO: arm_heavy_mb() part is missing (does additional flushing)
	// TODO: for DMA?!  ;;;  _update_: I think it might be for fec_rxq() !!
	asm volatile ("dsb st"); //wmb();
	txq->bd.cur = next_bdp;

	/* Trigger transmission start */
	//writel(0, txq->bd.reg_desc_active);
	// TODO: is reg_desc_active already a VA?
	io_write32((vaddr_t)txq->bd.reg_desc_active, 0);

	return 0;
}

static inline bool is_ipv4_pkt(struct sk_buff *skb)
{
	const struct ip *ip4;
	ip4 = (const struct ip *)bstgw_ethbuf_data_ptr(&skb->eth_buf, skb->eth_buf.l2_len);

	// this one only works bcs. router has already parsed the
	// eth type and set the packet_type accordingly for an outgoing packet
	// otherwise would need to parse ethernet header type instead (cf. worker.c)
	return RTE_ETH_IS_IPV4_HDR(skb->eth_buf.packet_type)
		&& ip4 && ip4->ip_v == 4;
}

static int
fec_enet_clear_csum(struct sk_buff *skb, struct net_device *ndev __unused)
{
	struct ip *ip4;
	const unsigned char *head;

	/* Only run for packets requiring a checksum. */
	if (skb_ip_summed(skb) != CHECKSUM_PARTIAL)
		return 0;
	
	ip4 = ip_hdr(skb);
	head = skb_head(skb);

// TODO: kick?
//	if (unlikely(skb_cow_head(skb, 0)))
//		return -1;

	if (is_ipv4_pkt(skb))
		ip4->ip_sum = 0;
	*(__sum16 *)(head + skb_csum_start(skb) + skb_csum_offset(skb)) = 0;

	return 0;
}



/* TODO: SG-I/O support */
static struct bufdesc *
fec_enet_txq_submit_frag_skb(struct fec_enet_priv_tx_q *txq,
			     struct sk_buff *skb,
			     struct net_device *ndev)
{
	(void)txq; (void)skb; (void)ndev;
	panic("scatter gather I/O not implemented");
}


static int fec_enet_txq_submit_skb(struct fec_enet_priv_tx_q *txq,
				   struct sk_buff *skb, struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int nr_frags = skb_nr_frags(skb);
	struct bufdesc *bdp, *last_bdp;
	void *bufaddr;
	dma_addr_t addr;
	unsigned short status;
	unsigned short buflen;
	unsigned int estatus = 0;
	unsigned int index;
	int entries_free;

	entries_free = fec_enet_get_free_txdesc_num(txq);
	// TODO: sg-I/O
	//TODO: shouldn't the check be against sth. else?
//#define MAX_SKB_FRAGS 16 // todo: might need to be +1 (also: UL)
	if (entries_free < /*MAX_SKB_FRAGS +*/ 1) {
//		if (net_ratelimit())
		return NETDEV_TX_BUSY;
	}

	/* Protocol checksum off-load for TCP and UDP. */
	if (fec_enet_clear_csum(skb, ndev)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	/* Fill in a Tx ring entry */
	bdp = txq->bd.cur;

	/* Set buffer length and buffer pointer */
	bufaddr = skb_data(skb);
	buflen = skb_headlen(skb);

	index = fec_enet_get_bd_index(bdp, &txq->bd);
	// TODO: we cannot fufill this alignment requirement at the moment,
	//		 because it would require us to have the Ethernet + IP headers
	//		 misaligned which would break the router + firewall
	if (((unsigned long) bufaddr) & fep->tx_align) {
		memcpy(txq->caligned_tx_bounce[index], skb_data(skb), buflen);
		bufaddr = txq->caligned_tx_bounce[index];
	}

	/* Push the data cache so the CPM does not get stale memory data. */
	addr = virt_to_phys(bufaddr);
	/* TODO: Can the missing invalidation cause a bad later overwrite?! */
	if( !addr || cache_operation(TEE_CACHECLEAN, bufaddr, buflen) != TEE_SUCCESS ) {
		dev_kfree_skb_any(skb);
//todo		if (net_ratelimit()) // <-- limits freq. the EMSG can be spammed?
			EMSG("Tx DMA memory map failed");
		return NETDEV_TX_OK;		
	}

	status = BD_ENET_TX_TC | BD_ENET_TX_READY |
			((bdp == txq->bd.last) ? BD_SC_WRAP : 0);
	if (nr_frags) {
		last_bdp = fec_enet_txq_submit_frag_skb(txq, skb, ndev);
		if (IS_ERR(last_bdp)) {
			// todo: invalidate cache?
//			dma_unmap_single(&fep->pdev->dev, addr,
//					 buflen, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	} else {
		last_bdp = bdp;
		status |= (BD_ENET_TX_INTR | BD_ENET_TX_LAST);
		if (fep->bufdesc_ex) {
			estatus = BD_ENET_TX_INT;
/* TODO: timestamp
			if (unlikely(skb_shinfo(skb)->tx_flags &
				SKBTX_HW_TSTAMP && fep->hwts_tx_en))
				estatus |= BD_ENET_TX_TS;
 */
		}
	}
	bdp->cbd_bufaddr = cpu_to_fec32(addr);
	bdp->cbd_datlen = cpu_to_fec16(buflen);

	if (fep->bufdesc_ex) {

		struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

/* TODO: timestamp
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP &&
			fep->hwts_tx_en))
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
*/

		if (skb_ip_summed(skb) == CHECKSUM_PARTIAL)
			estatus |= BD_ENET_TX_PINS | BD_ENET_TX_IINS;

		ebdp->cbd_bdu = 0;
		ebdp->cbd_esc = cpu_to_fec32(estatus);
	}

	index = fec_enet_get_bd_index(last_bdp, &txq->bd);
	last_bdp = fec_enet_get_nextdesc(last_bdp, &txq->bd);
	return fec_trigger_tx(txq, cpu_to_fec16(status), bdp, last_bdp,
			      skb, index);
}

static netdev_tx_t
fec_enet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	int entries_free;
//	unsigned short queue;
	struct fec_enet_priv_tx_q *txq;
	pktqueue_t *pq;
	int ret;


	// TODO: where is it set? (I don't see set_queue_mapping)

/* Our FEC only support a single queue
	queue = skb_get_queue_mapping(skb); */
	txq = fep->tx_queue[0 /*queue*/];
	pq = netdev_get_tx_queue(ndev, 0 /*queue*/);

	ret = fec_enet_txq_submit_skb(txq, skb, ndev);
	if (ret)
		return ret;

	entries_free = fec_enet_get_free_txdesc_num(txq);
	if (entries_free <= txq->tx_stop_threshold)
		netif_tx_stop_queue(pq);


	return NETDEV_TX_OK;
}

static void reset_tx_queue(struct fec_enet_private *fep __unused,
			   struct fec_enet_priv_tx_q *txq)
{
	struct bufdesc *bdp = txq->bd.base;
	unsigned int i;

	txq->bd.cur = bdp;
	txq->pending_tx = bdp;

	for (i = 0; i < txq->bd.ring_size; i++) {
		/* Initialize the BD for every fragment in the page. */
		if (bdp->cbd_bufaddr) {
			// todo: invalidate cache?
			bdp->cbd_bufaddr = cpu_to_fec32(0);
		}
		if (txq->tx_skbuff[i]) {
			dev_kfree_skb_any(txq->tx_skbuff[i]);
			txq->tx_skbuff[i] = NULL;
		}
		bdp->cbd_sc = cpu_to_fec16((bdp == txq->bd.last) ?
					   BD_SC_WRAP : 0);
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
	}
}

/* Init RX & TX buffer descriptors
 */
static void fec_enet_bd_init(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	struct fec_enet_priv_rx_q *rxq;
	struct bufdesc *bdp;
	unsigned int i;
	unsigned int q;
	unsigned int status;

	// note: we currently only have 1 rxq
	for (q = 0; q < fep->num_rx_queues; q++) {
		/* Initialize the receive buffer descriptors. */
		rxq = fep->rx_queue[q];
		bdp = rxq->bd.base;

		for (i = 0; i < rxq->bd.ring_size; i++) {
			/* Initialize the BD for every fragment in the page. */
			status = bdp->cbd_bufaddr ? BD_ENET_RX_EMPTY : 0;
			if (bdp == rxq->bd.last)
				status |= BD_SC_WRAP;
			bdp->cbd_sc = cpu_to_fec16(status);
			bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
		}
		rxq->bd.cur = rxq->bd.base;
	}

	// note: we currently only have 1 txq
	for (q = 0; q < fep->num_tx_queues; q++)
		reset_tx_queue(fep, fep->tx_queue[q]);
}

static void fec_enet_active_rxring(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;

	// TODO: VA?
	for (i = 0; i < fep->num_rx_queues; i++)
		io_write32((vaddr_t)fep->rx_queue[i]->bd.reg_desc_active, 0);
		//writel(0, fep->rx_queue[i]->bd.reg_desc_active);
}

static void fec_enet_enable_ring(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_enet_priv_tx_q *txq;
	struct fec_enet_priv_rx_q *rxq;
	unsigned int i;

	for (i = 0; i < fep->num_rx_queues; i++) {
		rxq = fep->rx_queue[i];
		//writel(rxq->bd.dma, fep->hwp + FEC_R_DES_START(i));
		//writel(PKT_MAXBUF_SIZE, fep->hwp + FEC_R_BUFF_SIZE(i));
		// TODO: VA?
		io_write32((vaddr_t)fep->hwp + FEC_R_DES_START(i), rxq->bd.dma);
		io_write32((vaddr_t)fep->hwp + FEC_R_BUFF_SIZE(i), PKT_MAXBUF_SIZE);

		/* enable DMA1/2 */
		// TODO: VA?
		if (i)
			io_write32((vaddr_t)fep->hwp + FEC_RCMR(i),
					RCMR_MATCHEN | RCMR_CMP(i));
			//writel(RCMR_MATCHEN | RCMR_CMP(i),
			//       fep->hwp + FEC_RCMR(i));
	}

	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = fep->tx_queue[i];
		//writel(txq->bd.dma, fep->hwp + FEC_X_DES_START(i));
		// TODO: VA?
		io_write32((vaddr_t)fep->hwp + FEC_X_DES_START(i), txq->bd.dma);

		/* enable DMA1/2 */
		// TODO: VA?
		if (i)
			io_write32((vaddr_t)fep->hwp + FEC_DMA_CFG(i),
					DMA_CLASS_EN | IDLE_SLOPE(i));
			//writel(DMA_CLASS_EN | IDLE_SLOPE(i),
			//       fep->hwp + FEC_DMA_CFG(i));
	}
}

static void fec_enet_reset_skb(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct fec_enet_priv_tx_q *txq;
	unsigned int i, j;

	// note: we currently only have 1 txq
	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = fep->tx_queue[i];

		for (j = 0; j < txq->bd.ring_size; j++) {
			if (txq->tx_skbuff[j]) {
				dev_kfree_skb_any(txq->tx_skbuff[j]);
				txq->tx_skbuff[j] = NULL;
			}
		}
	}
}

/*
 * This function is called to start or restart the FEC during a link
 * change, transmit timeout, or to reconfigure the FEC.  The network
 * packet processing for this device must be stopped before this call.
 */
//static void
//fec_restart(struct net_device *ndev)

void fec_restart_sw_0(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	fep->sfec_state.rcntl = OPT_FRAME_SIZE | 0x04;
	fep->sfec_state.ecntl = FEC_ENET_ETHEREN; /* ETHEREN */
}

void fec_restart_sw_1(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 temp_mac[2];
	memcpy(&temp_mac, ndev->hwaddr.addr_bytes, RTE_ETHER_ADDR_LEN);
	io_write32(fep->hwp + FEC_ADDR_LOW,
			(/*__force*/ u32)cpu_to_be32(temp_mac[0]));
	io_write32(fep->hwp + FEC_ADDR_HIGH,
			(/*__force*/ u32)cpu_to_be32(temp_mac[1]));
}

void fec_restart_sw_2(struct net_device *ndev) {
	fec_enet_bd_init(ndev);
	fec_enet_enable_ring(ndev);

	/* Reset tx SKB buffers. */
	fec_enet_reset_skb(ndev);
}
void fec_restart_sw_3(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	u32 val;
	if (fep->quirks & FEC_QUIRK_HAS_RACC) {
		val = io_read32(fep->hwp + FEC_RACC);
		/* align IP header */
		val |= FEC_RACC_SHIFT16;
		if (fep->csum_flags & FLAG_RX_CSUM_ENABLED)
			/* set RX checksum */
			val |= FEC_RACC_OPTIONS;
		else
			val &= ~FEC_RACC_OPTIONS;
		io_write32(fep->hwp + FEC_RACC, val);
		io_write32(fep->hwp + FEC_FTRL, PKT_MAXBUF_SIZE);
	}
	io_write32(fep->hwp + FEC_FTRL, PKT_MAXBUF_SIZE);
}
void fec_restart_sw_4_opt(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	
	io_write32(fep->hwp + FEC_R_FIFO_RSFL, FEC_ENET_RSFL_V);
	io_write32(fep->hwp + FEC_R_FIFO_RAEM, FEC_ENET_RAEM_V);
	io_write32(fep->hwp + FEC_R_FIFO_RAFL, FEC_ENET_RAFL_V);

	/* OPD */
	io_write32(fep->hwp + FEC_OPD, FEC_ENET_OPD_V);
}
void fec_restart_sw_5(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	/* Setup multicast filter. */
	set_multicast_list(ndev);
	io_write32(fep->hwp + FEC_HASH_TABLE_HIGH, 0);
	io_write32(fep->hwp + FEC_HASH_TABLE_LOW, 0);

	if (fep->quirks & FEC_QUIRK_ENET_MAC) {
		/* enable ENET endian swap */
		fep->sfec_state.ecntl |= (1 << 8);
		/* enable ENET store and forward mode */
		io_write32(fep->hwp + FEC_X_WMRK, 1 << 8);
	}

	// note: disabled atm
	if (fep->bufdesc_ex)
		fep->sfec_state.ecntl |= (1 << 4);

	/* Enable the MIB statistic event counters */
	// TODO: do we need that?!
	io_write32(fep->hwp + FEC_MIB_CTRLSTAT, 0 << 31);
}
void fec_restart_sw_6(struct net_device *ndev) {
	fec_enet_active_rxring(ndev);
}



static int fec_txq(struct net_device *ndev, struct fec_enet_priv_tx_q *txq)
{
	struct  fec_enet_private *fep = netdev_priv(ndev);
	(void)fep;
	
	struct bufdesc *bdp;
	unsigned short status;
	struct	sk_buff	*skb;
	pktqueue_t *pq;
	int	index = 0;
	int	entries_free;

	unsigned int txc_packets = 0, bd_processed = 0;

	pq = netdev_get_tx_queue(ndev, txq->bd.qid);
	bdp = txq->pending_tx;

	while (bdp != READ_ONCE(txq->bd.cur)) {
		/* Order the load of bd.cur and cbd_sc */
		dsb(); // rmb();
		status = fec16_to_cpu(READ_ONCE(bdp->cbd_sc));
		if (status & BD_ENET_TX_READY) {
			//if (!readl(txq->bd.reg_desc_active)) {
			if (!io_read32((vaddr_t)txq->bd.reg_desc_active)) {
				/* ERR006358 has hit, restart tx */
#ifdef BSTGW_DBG_STATS
				hit_err006358++;
#endif
				//writel(0, txq->bd.reg_desc_active);
				io_write32((vaddr_t)txq->bd.reg_desc_active, 0);
			}
			// todo: Does this count as a packet or not?
			break;
		}
		bdp->cbd_sc = cpu_to_fec16((bdp == txq->bd.last) ?
					   BD_SC_WRAP : 0);

		index = fec_enet_get_bd_index(bdp, &txq->bd);

		skb = txq->tx_skbuff[index];
		txq->tx_skbuff[index] = NULL;
		// todo: invalidate cache?
		bdp->cbd_bufaddr = cpu_to_fec32(0);
		if (!skb)
			goto skb_done;

		txc_packets++;

		/* Check for errors. */
		// TODO: add support for error stats
		if(unlikely( status & (BD_ENET_TX_HB | BD_ENET_TX_LC |
				   BD_ENET_TX_RL | BD_ENET_TX_UN |
				   BD_ENET_TX_CSL) )) {
			//ndev->stats.tx_errors++;
			EMSG("tx_error");
			if (status & BD_ENET_TX_HB)  /* No heartbeat */
				//ndev->stats.tx_heartbeat_errors++;
				EMSG("tx_heartbeat_error");
			if (status & BD_ENET_TX_LC)  /* Late collision */
				//ndev->stats.tx_window_errors++;
				EMSG("tx_window_error");
			if (status & BD_ENET_TX_RL)  /* Retrans limit */
				//ndev->stats.tx_aborted_errors++;
				EMSG("tx_aborted_error");
			if (status & BD_ENET_TX_UN)  /* Underrun */
				//ndev->stats.tx_fifo_errors++;
				EMSG("tx_fifo_error");
			if (status & BD_ENET_TX_CSL) /* Carrier lost */
				//ndev->stats.tx_carrier_errors++;
				EMSG("tx_carrier_error");
		} else {
			//ndev->stats.tx_packets++;
			//ndev->stats.tx_bytes += skb->len;
		}


		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if(unlikely( status & BD_ENET_TX_DEF ))
			EMSG("collision");
			//ndev->stats.collisions++;

		/* Free the sk buffer associated with this last transmit */
		dev_kfree_skb_any(skb);
skb_done:
		/* Update pointer to next buffer descriptor to be transmitted */
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);

		/* Make sure the update to bdp and tx_skbuff are performed
		 * before pending_tx
		 */
		//TODO: arm_heavy_mb() part is missing (does additional flushing)
		// TODO: for DMA?!
		asm volatile ("dsb st"); //wmb();
		txq->pending_tx = bdp;

		/* Since we have freed up a buffer, the ring is no longer full
		 */
		if (netif_tx_queue_stopped(pq)) {
			entries_free = fec_enet_get_free_txdesc_num(txq);
			if (entries_free >= txq->tx_wake_threshold)
				netif_tx_wake_queue(ndev, txq->bd.qid);
		}

		bd_processed++;
		if (bd_processed >= BSTGW_TXC_BUDGET) break;
	}

#ifdef BSTGW_DBG_STATS
	if (txc_packets > max_tx_complete)
		max_tx_complete = txc_packets;
#endif

	return bd_processed;
}


static int
fec_enet_new_rxbdp(struct net_device *ndev, struct bufdesc *bdp, struct sk_buff *skb)
{
	struct  fec_enet_private *fep = netdev_priv(ndev);
	int off;
	dma_addr_t dmabuf;

	// Note: as RACC_SHIFT16 will later result in a 2B alignment of the ethernet
	//       frame it is fine to allow the shift
	off = ((unsigned long)skb_data(skb)) & fep->rx_align;
	if (off)
		skb_reserve(skb, fep->rx_align + 1 - off);

	dmabuf = virt_to_phys(skb_data(skb));
	// do we need a clean, too?
	if( !dmabuf || (cache_operation(TEE_CACHEINVALIDATE, skb_data(skb),
		FEC_ENET_RX_FRSIZE - fep->rx_align) != TEE_SUCCESS) ) {
//todo		if (net_ratelimit())
			EMSG("Rx DMA memory map failed");
		return -ENOMEM;
	}

	bdp->cbd_bufaddr = cpu_to_fec32(dmabuf);

	return 0;
}

static bool fec_enet_copybreak(struct net_device *ndev, struct sk_buff **skb,
			       struct bufdesc *bdp, u32 length)
{
	struct  fec_enet_private *fep = netdev_priv(ndev);
	struct sk_buff *new_skb;

	if (length > fep->rx_copybreak)
		return false;

	new_skb = netdev_alloc_skb(ndev, length);
	if (!new_skb) {
        EMSG("OOM");
		return false;
    }

	// Note: we achieved about +8% rx perf. when using roundup2(length, 64)
	if( cache_operation(TEE_CACHEINVALIDATE,
			phys_to_virt(fec32_to_cpu(bdp->cbd_bufaddr), MEM_AREA_TEE_RAM),
			FEC_ENET_RX_FRSIZE - fep->rx_align) != TEE_SUCCESS ) {
		EMSG("ERROR: cache invalidation failed");
	}

	memcpy(skb_data(new_skb), skb_data((*skb)), length);
	*skb = new_skb;
	// note: ->len adjustement is later done via skb_put()

	return true;
}




/* During a receive, the bd_rx.cur points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int fec_rxq(struct net_device *ndev, struct fec_enet_priv_rx_q *rxq,
		   int budget)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc *bdp;
	unsigned short status;
	struct  sk_buff *skb_new = NULL;
	struct  sk_buff *skb;
	ushort	pkt_len;
	u8 *data; //__u8
	int	pkt_received = 0;
	struct	bufdesc_ex *ebdp = NULL;
	bool	vlan_packet_rcvd = false;
	//todo: u16	vlan_tag;
	int	index = 0;
	bool	is_copybreak;
	pktqueue_t *pq = &fep->in_queue;

	uint32_t exceptions;
	size_t nbufs;


	if(unlikely( budget < 0 )) return 0;

	/* New: burst receive */
	// TODO: is this '2*' a deprecated leftover?
	bstgw_ethbuf_t *rx_burst_bufs[2*BSTGW_RX_BURST_SZ];
	unsigned int ref_burst_sz, burst_sz, nburst_pkts = 0;

	ref_burst_sz = BSTGW_RX_BURST_SZ; //router_in_burst_size(ndev, NULL);
	burst_sz = ((uint32_t)budget <= ref_burst_sz)?(uint32_t)budget:ref_burst_sz;


	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = rxq->bd.cur;

	while (!((status = fec16_to_cpu(bdp->cbd_sc)) & BD_ENET_RX_EMPTY)) {

		if (pkt_received >= budget)
			break;
		pkt_received++;

		/* Check for errors. */
		// TODO: add support for error stats
		status ^= BD_ENET_RX_LAST;
		if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH | BD_ENET_RX_NO |
			   BD_ENET_RX_CR | BD_ENET_RX_OV | BD_ENET_RX_LAST |
			   BD_ENET_RX_CL)) {
			//ndev->stats.rx_errors++;
//todo			EMSG("rx_error");
			if (status & BD_ENET_RX_OV) {
				/* FIFO overrun */
				//ndev->stats.rx_fifo_errors++;
//todo				EMSG("rx_fifo_error");
				goto rx_processing_done;
			}
			if (status & (BD_ENET_RX_LG | BD_ENET_RX_SH
						| BD_ENET_RX_LAST)) {
				/* Frame too long or too short. */
				//ndev->stats.rx_length_errors++;
				EMSG("rx_length_error");
				if (status & BD_ENET_RX_LAST)
					EMSG("rcv is not +last");
			}
			if (status & BD_ENET_RX_CR)	/* CRC Error */
				//ndev->stats.rx_crc_errors++;
				EMSG("rx_crc_error");
			/* Report late collisions as a frame error. */
			if (status & (BD_ENET_RX_NO | BD_ENET_RX_CL))
				//ndev->stats.rx_frame_errors++;
				EMSG("rx_frame_error");
			goto rx_processing_done;
		}


		/* Process the incoming frame. */
		//ndev->stats.rx_packets++; //todo
		pkt_len = fec16_to_cpu(bdp->cbd_datlen);
		//ndev->stats.rx_bytes += pkt_len; //todo


		index = fec_enet_get_bd_index(bdp, &rxq->bd);
		skb = rxq->rx_skbuff[index];


		/* The packet length includes FCS, but we don't want to
		 * include that when passing upstream as it messes up
		 * bridging applications.
		 */
		is_copybreak = fec_enet_copybreak(ndev, &skb, bdp, pkt_len - 4);
		// todo: we currently only use the copybreak path, and !is_copybreak
		//       would mean OOM during ethernet buffer allocation
		if (!is_copybreak) panic("fec_enet_copybreak() failed");
#if 0
			skb_new = netdev_alloc_skb(ndev, FEC_ENET_RX_FRSIZE);
			if (unlikely(!skb_new)) {
				//todo: ndev->stats.rx_dropped++;
				EMSG("rx_dropped (OOM)");
				goto rx_processing_done;
			}
			// todo: invalidate cache?
			if( cache_operation(TEE_CACHEINVALIDATE,
				phys_to_virt(fec32_to_cpu(bdp->cbd_bufaddr), MEM_AREA_TEE_RAM),
				FEC_ENET_RX_FRSIZE - fep->rx_align) != TEE_SUCCESS ) {
				EMSG("ERROR: cache invalidation failed");
			}
#if 0
			dma_unmap_single(&fep->pdev->dev,
					 fec32_to_cpu(bdp->cbd_bufaddr),
					 FEC_ENET_RX_FRSIZE - fep->rx_align,
					 DMA_FROM_DEVICE);
#endif
		}
#endif

		// TODO:!
		//prefetch(skb->data - NET_IP_ALIGN); // cache-line prefetch for read
//		prefetch(skb_data(skb));

		skb_put(skb, pkt_len - 4);
		data = skb_data(skb);

		// bcs. of RACC_SHIFT16 (prepends 2 Bytes to align ethernet-ip headers)
		if (fep->quirks & FEC_QUIRK_HAS_RACC)
			data = skb_pull_inline(skb, 2);

		/* Extract the enhanced buffer descriptor */
		ebdp = NULL;
		if (fep->bufdesc_ex)
			ebdp = (struct bufdesc_ex *)bdp;

		/* If this is a VLAN packet remove the VLAN Tag */
		vlan_packet_rcvd = false;

		// TODO: seems to cut the ethernet header?!
		// -- that is something we don't want, bcs. the router
		//    works on ethernet frames, not IP packets!
		// TODO: also only user got kicked
		// TODO: also we set it in the router
		//skb->protocol = eth_type_trans(skb, ndev);


		if (fep->bufdesc_ex &&
		    (fep->csum_flags & FLAG_RX_CSUM_ENABLED)) {
			if (!(ebdp->cbd_esc & cpu_to_fec32(FLAG_RX_CSUM_ERROR))) {
				/* don't check it */
				skb_ip_summed(skb) = CHECKSUM_UNNECESSARY;
			} else {
				skb_checksum_none_assert(skb);
			}
		}



		// todo: set to actual value (if add VLAN, also move this)
		skb->eth_buf.packet_type = RTE_PTYPE_L2_MASK;

#ifdef BSTGW_FEC_IMX_DEBUG
		bstgw_ethbuf_align_check(&skb->eth_buf);
#endif


	
#ifdef BSTGW_FEC_IMX_DEBUG
		static_assert(offsetof(struct sk_buff, eth_buf) == 0, "eth_buf must be at offset 0");
#endif
		/* New:  burst/batch receive */
		rx_burst_bufs[nburst_pkts] = &skb->eth_buf;
		nburst_pkts += 1;


		//napi_gro_receive(&fep->rx_napi, skb);
//#ifdef BSTGW_FEC_IMX_DEBUG
//		DMSG("after napi_gro_receive");
//#endif




		if (is_copybreak) {
			// TODO: which op? is CACHECLEAN [for_device] enough/correct?
			// Unclear to me why this is necessary in our case.
			// We (and vanilla) achieve about +100 Mbps when commenting it out.
			// When just adapting the range to roundup2(pkt_len, 64), we +8%
			// (which was about 20-30Mbps)
			if( cache_operation(TEE_CACHEINVALIDATE,
					phys_to_virt(fec32_to_cpu(bdp->cbd_bufaddr), MEM_AREA_TEE_RAM),
					FEC_ENET_RX_FRSIZE - fep->rx_align) != TEE_SUCCESS ) {
				EMSG("ERROR: cache invalidation failed");
			}
		} else {
			panic("Invalid path!");
			//rxq->rx_skbuff[index] = skb_new;
			//fec_enet_new_rxbdp(ndev, bdp, skb_new);
		}

rx_processing_done:
		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;

			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_RX_INT);
			ebdp->cbd_prot = 0;
			ebdp->cbd_bdu = 0;
		}

		/* Make sure the updates to rest of the descriptor are
		 * performed before transferring ownership.
		 */
		asm volatile ("dmb oshst"); // dma_wmb();
		bdp->cbd_sc = cpu_to_fec16(BD_ENET_RX_EMPTY |
				((bdp == rxq->bd.last) ? BD_SC_WRAP : 0));

		/* Update BD pointer to next entry */
		bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);

		/* Doing this here will keep the FEC running while we process
		 * incoming frames.  On a heavily loaded network, we should be
		 * able to keep up at the expense of system resources.
		 */
		//writel(0, rxq->bd.reg_desc_active);
		io_write32((vaddr_t)rxq->bd.reg_desc_active, 0);


		/* Batch processing */
		if (nburst_pkts >= burst_sz) {
//			int nrcv = ndev->rx_handler(ndev, rx_burst_bufs, nburst_pkts);
#ifdef BSTGW_FEC_IMX_DEBUG
			//todo
//			if (nrcv < 0) EMSG("Error during FEC receive (err: %d)", nrcv);
#endif

			exceptions = cpu_spin_lock_xsave(&pq->slock);
			nbufs = bstgw_pktq_locked_bulk_add_buff(pq, rx_burst_bufs, nburst_pkts);
			cpu_spin_unlock_xrestore(&pq->slock, exceptions);

			// clean those that didn't fit
			for (; nbufs < nburst_pkts; nbufs++)
				bstgw_ethpool_buf_free(rx_burst_bufs[nbufs]);

			if(!atomic_flag_test_and_set(&fep->no_kick))
				napi_schedule(&fep->route_tx_napi);
			nburst_pkts = 0;
		}
	}

	/* New: burst receive */
	if (nburst_pkts > 0) {
//		int nrcv = ndev->rx_handler(ndev, rx_burst_bufs, nburst_pkts);
#ifdef BSTGW_FEC_IMX_DEBUG
		//todo
//		if (nrcv < 0) EMSG("Error during FEC receive (err: %d)", nrcv);
#endif
		exceptions = cpu_spin_lock_xsave(&pq->slock);
		nbufs = bstgw_pktq_locked_bulk_add_buff(pq, rx_burst_bufs, nburst_pkts);
		cpu_spin_unlock_xrestore(&pq->slock, exceptions);

		// clean those that didn't fit
		for (; nbufs < nburst_pkts; nbufs++)
			bstgw_ethpool_buf_free(rx_burst_bufs[nbufs]);

		if(!atomic_flag_test_and_set(&fep->no_kick))
			napi_schedule(&fep->route_tx_napi);
	}

#ifdef BSTGW_FEC_IMX_DEBUG
		DMSG("rx queue exit with %d received packets (incl. dropped ones)",
		pkt_received);
#endif

	rxq->bd.cur = bdp;
	return pkt_received;
}





// TODO: this actually belongs into the SW,
//		 but some parts might also belong to the NW
//		 which we could handle via interrupt forwarding.

//fec_enet_interrupt(int irq, void *dev_id)
static irqreturn_t
fec_enet_interrupt(struct irq_handler *h)
{
	struct fec_enet_private *fep = h->data;
	irqreturn_t ret = ITRR_NONE;
#ifdef BSTGW_DBG_STATS
	fec_itr_calls++;
#endif


	//uint eir = readl(fep->hwp + FEC_IEVENT);
	//uint int_events = eir & readl(fep->hwp + FEC_IMASK);
	uint eir = io_read32((vaddr_t)fep->hwp + FEC_IEVENT);
	uint imask = io_read32((vaddr_t)fep->hwp + FEC_IMASK);
	uint int_events = eir & imask;

	// TODO: unclear why this sometimes happens (on high TX load)
	if(unlikely( !int_events )) {
		/* PROBLEM:  if we just let if fall through and return ITRR_NONE,
		 * OP-TEE will disable the unprocessed IRQ line. */
		return ITRR_HANDLED;
	}

	struct napi_struct *n = &fep->rx_napi;
	if (int_events & (FEC_ENET_RXF | FEC_ENET_TXF)) {
		uint32_t exceptions = cpu_spin_lock_xsave(&n->slock);
		if (napi_schedule_prep_locked(n)) {
			/* Disable the NAPI interrupts */
			//writel(FEC_NAPI_IMASK, fep->hwp + FEC_IMASK);
			io_write32((vaddr_t)fep->hwp + FEC_IMASK, FEC_NAPI_IMASK);
			__napi_schedule_locked(n);
			int_events &= ~(FEC_ENET_RXF | FEC_ENET_TXF);
			ret = ITRR_HANDLED;
		} else {
			fep->events |= int_events;
			EMSG("couldn't schedule NAPI");
		}
		cpu_spin_unlock_xrestore(&n->slock, exceptions);
	}

	if (int_events) {
		ret = ITRR_HANDLED;
		// Q: why this write? is it a kind of ACK?
		//writel(int_events, fep->hwp + FEC_IEVENT);
		io_write32((vaddr_t)fep->hwp + FEC_IEVENT, int_events);

		// forward interrupt to NW
		if (int_events & FEC_ENET_MII) {
			//complete(&fep->mdio_done);
			irq_raise(fep->irq_pass);
		}
	}

	return ret;
}



static void bstgw_fec_schedule_xmit(struct fec_enet_private *fep) {
	if(!atomic_flag_test_and_set(&fep->no_kick))
		napi_schedule(&fep->route_tx_napi);
}




static int fec_route_xmit_napi(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->net_dev;
	struct fec_enet_private *fep = netdev_priv(ndev);
	int pkts = 0;
	int nsent, nrcv;
	bool no_egress = false, no_input = false;
	struct fec_enet_priv_tx_q *txq = fep->tx_queue[0 /*queue*/];;

	pktqueue_t *pq = &fep->in_queue;
	uint32_t exceptions;


	do {
		nsent = 0;
		nrcv = 0;

		/* Receive Part */
		if (!no_input) {
			exceptions = cpu_spin_lock_xsave(&pq->slock);
			unsigned pktburst = bstgw_pktq_locked_contig_bufs(pq);
			cpu_spin_unlock_xrestore(&pq->slock, exceptions);
			if (pktburst) {
				pktburst = MIN(pktburst, 3 * BSTGW_RX_BURST_SZ);
				nrcv = ndev->rx_handler(ndev, &pq->pkt[pq->next_read], pktburst);
				pkts += nrcv;
				exceptions = cpu_spin_lock_xsave(&pq->slock);
				bstgw_pktq_locked_drop_bufs(pq, pktburst);
				cpu_spin_unlock_xrestore(&pq->slock, exceptions);
			} else {
				nrcv = 0;
			}
			no_input = (nrcv == 0);
		} else
			no_input = false;

		/* Sending Part */
		if (!no_egress) {
			nsent = router_pktq_tx(ndev);
			no_egress = (nsent == 0);
			if(unlikely( nsent < 0 )) panic("egress error");
#ifdef BSTGW_FEC_IMX_DEBUG
			if (!nsent) DMSG("nothing transmitted");
			else DMSG("successfully transmitted %d packets", nsent);
#endif
			pkts += nsent;
		} else {
			no_egress = false;
		}

	} while(nrcv || nsent);

	if(napi_complete(napi)) {
		atomic_flag_clear(&fep->no_kick);
		bool is_empty;

		exceptions = thread_mask_exceptions(THREAD_EXCP_ALL);

		/* Check if there was a race condition, s.t. we lost a notification */
		if( !netif_tx_queue_stopped(ndev->egress_queue) ) {
			cpu_spin_lock(&ndev->egress_queue->slock);
			is_empty |= bstgw_pktq_locked_is_empty(ndev->egress_queue);
			cpu_spin_unlock(&ndev->egress_queue->slock);
			if (!is_empty) goto xmit_rerun;
		}

		// new input packets?
		cpu_spin_lock(&pq->slock);
		is_empty = bstgw_pktq_locked_is_empty(pq);
		cpu_spin_unlock(&pq->slock);

		if (!is_empty) {
xmit_rerun:
			// schedule if not already indicated it has been done
			if(!atomic_flag_test_and_set(&fep->no_kick))
				napi_schedule(napi);
		}

		thread_unmask_exceptions(exceptions);

	} else {
		atomic_flag_test_and_set(&fep->no_kick);
	}


#ifdef BSTGW_DBG_STATS
	xmit_napi_leaves++;
#endif
	return pkts;
}



static int fec_enet_napi_q1(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->net_dev;
	struct fec_enet_private *fep = netdev_priv(ndev);
	int pkts = 0, nrecv, nfreed;
	uint events;


	do {

		//events = readl(fep->hwp + FEC_IEVENT);
		events = io_read32((vaddr_t)fep->hwp + FEC_IEVENT);
		if (fep->events) {
			events |= fep->events;
			fep->events = 0;
		}
		events &= FEC_ENET_RXF_0 | FEC_ENET_TXF_0;

		if (!events ) { //&& (!has_egr_packets || no_egress)) {
			if (budget) {
				/* TODO: if !napi_complete() shouldn't re-arm interrupts yet,
				 * becaue it will get called another time. */
//#if 0
				if (napi_complete(napi)) {
					//writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);
					io_write32((vaddr_t)fep->hwp + FEC_IMASK, FEC_DEFAULT_IMASK);
				} else {
				}
//#endif
			}

#ifdef BSTGW_DBG_STATS
			rx_napi_leaves++;
#endif
			return pkts;
		}

		//writel(events, fep->hwp + FEC_IEVENT);
		io_write32((vaddr_t)fep->hwp + FEC_IEVENT, events);
		/* don't check FEC_ENET_RXF_0, to recover from a full queue
		 * but bit clear condition
		 */

		// restrict fec_rxq() budget to avoid starvation of fec_txq()
		pkts += (nrecv = fec_rxq(ndev, fep->rx_queue[0], BSTGW_RX_BUDGET));
		if (nrecv >= BSTGW_RX_BUDGET) fep->events |= FEC_ENET_RXF_0;

		/* note: TXF could have not been set before fec_rxq(), so in some sit.
		 * it might require 2 fec_rxq() rounds until txq is called */
		if (events & FEC_ENET_TXF_0) {
			nfreed = fec_txq(ndev, fep->tx_queue[0]);
			if (nfreed >= BSTGW_TXC_BUDGET) fep->events |= FEC_ENET_TXF_0;
		}


	} while (pkts < budget);
	fep->events |= FEC_ENET_RXF_0;	/* save for next callback */


#ifdef BSTGW_DBG_STATS
	rx_napi_leaves++;
#endif
	return pkts;
}


/* ------------------------------------------------------------------------- */
// TODO: Highly incomplete
static void fec_get_mac(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
//todo:	struct fec_platform_data *pdata = dev_get_platdata(&fep->pdev->dev);
	//unsigned char *iap, tmpaddr[RTE_ETHER_ADDR_LEN] = {0};
	struct rte_ether_addr *iap, tmpaddr = {0};

	/* try to get mac address in following order:
	 *
	 * 1) statically defined macaddr (TODO: do we in boot config script?)
	 */
	iap = &macaddr;

	/* 2) FEC mac registers set by bootloader
	 */
	if (!rte_is_valid_assigned_ether_addr(iap)) {
		*((__be32 *)&tmpaddr.addr_bytes[0]) =
			cpu_to_be32(io_read32((vaddr_t)fep->hwp + FEC_ADDR_LOW));
			//cpu_to_be32(readl(fep->hwp + FEC_ADDR_LOW));
		*((__be16 *)&tmpaddr.addr_bytes[4]) =
			cpu_to_be16(io_read32((vaddr_t)fep->hwp + FEC_ADDR_HIGH) >> 16);
			//cpu_to_be16(readl(fep->hwp + FEC_ADDR_HIGH) >> 16);
		iap = &tmpaddr;
	}

	/* 3) TODO: from device tree data
	 */
	/* 4) TODO: from flash or fuse (via platform data)
	 */
	/* 5) TODO: random mac address
	 */

	if (!rte_is_valid_assigned_ether_addr(iap)) {
		panic("Getting a valid MAC failed (FIXME: incomplete impl.)");
	}

	rte_ether_addr_copy(iap, &ndev->hwaddr);

	/* Adjust MAC if using macaddr */
#if 0 //todo?
	if (iap == &macaddr)
		 ndev->dev_addr[RTE_ETHER_ADDR_LEN-1] = macaddr[RTE_ETHER_ADDR_LEN-1] + fep->dev_id;
#endif
}


/* ------------------------------------------------------------------------- */

/*
 * Phy section (TODO: unclear how to split at the moment)
 */
void fec_enet_adjust_link_sw_restart_pre(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	/* TODO: order?! */
	netif_tx_stop_queue(ndev->egress_queue); // new
	napi_disable(&fep->rx_napi);
	napi_disable(&fep->route_tx_napi);
}
void fec_enet_adjust_link_sw_restart_post(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	// order fine?
	//netif_tx_wake_all_queues(ndev);
	//netif_tx_wake_queue(ndev, 0); // we only have 1
	napi_enable(&fep->route_tx_napi);
	napi_enable(&fep->rx_napi);
	netif_tx_wake_queue(ndev, 0); // new order
}

void fec_enet_adjust_link_sw_stop_pre(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	/* TODO: order ?! */
	if (fep->sfec_state.primary_st == SFEC_PRI_SHUTTING_DOWN)
		netif_tx_disable(ndev);
	else
		netif_tx_stop_queue(ndev->egress_queue); // new
	napi_disable(&fep->rx_napi);
	napi_disable(&fep->route_tx_napi);
}
void fec_enet_adjust_link_sw_stop_post(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	if (fep->sfec_state.primary_st == SFEC_PRI_SHUTTING_DOWN) {
		return;
	}
	napi_enable(&fep->route_tx_napi);
	napi_enable(&fep->rx_napi);
}


static void fec_enet_free_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;
	struct fec_enet_priv_tx_q *txq;
	struct fec_enet_priv_rx_q *rxq;
	unsigned int q;

	for (q = 0; q < fep->num_rx_queues; q++) {
		rxq = fep->rx_queue[q];
		bdp = rxq->bd.base;
		for (i = 0; i < rxq->bd.ring_size; i++) {
			skb = rxq->rx_skbuff[i];
			rxq->rx_skbuff[i] = NULL;
			if (skb) {
				// todo: invalidate cache?
				bdp->cbd_bufaddr = cpu_to_fec32(0);
				dev_kfree_skb(skb);
			}
			bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
		}
	}

	for (q = 0; q < fep->num_tx_queues; q++) {
		txq = fep->tx_queue[q];
		reset_tx_queue(fep, txq);
		for (i = 0; i < txq->bd.ring_size; i++) {
			linux_kfree(txq->tx_bounce[i]);
			txq->tx_bounce[i] = NULL;
			txq->caligned_tx_bounce[i] = NULL;
		}
	}
}

static void fec_enet_free_queue(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;

	for (i = 0; i < fep->num_rx_queues; i++)
		linux_kfree(fep->rx_queue[i]);
	for (i = 0; i < fep->num_tx_queues; i++)
		linux_kfree(fep->tx_queue[i]);
}

static int fec_enet_alloc_queue(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	int ret = 0;
	struct fec_enet_priv_tx_q *txq;

	for (i = 0; i < fep->num_tx_queues; i++) {
		txq = linux_kzalloc(sizeof(*txq), GFP_KERNEL);
		if (!txq) {
			ret = -ENOMEM;
			goto alloc_failed;
		}

		fep->tx_queue[i] = txq;
		txq->bd.ring_size = TX_RING_SIZE;
		fep->total_tx_ring_size += fep->tx_queue[i]->bd.ring_size;

		txq->tx_stop_threshold = 0; /*FEC_MAX_SKB_DESCS;*/ // TODO: !
		txq->tx_wake_threshold =
			(txq->bd.ring_size - txq->tx_stop_threshold) / 2;
			//BSTGW_TX_MIN_FREE_BD;
	}

	for (i = 0; i < fep->num_rx_queues; i++) {
		fep->rx_queue[i] = linux_kzalloc(sizeof(*fep->rx_queue[i]),
					   GFP_KERNEL);
		if (!fep->rx_queue[i]) {
			ret = -ENOMEM;
			goto alloc_failed;
		}

		fep->rx_queue[i]->bd.ring_size = RX_RING_SIZE;
		fep->total_rx_ring_size += fep->rx_queue[i]->bd.ring_size;
	}
	return ret;

alloc_failed:
	fec_enet_free_queue(ndev);
	return ret;
}

static int
fec_enet_alloc_rxq_buffers(struct net_device *ndev, unsigned int queue)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct sk_buff *skb;
	struct bufdesc	*bdp;
	struct fec_enet_priv_rx_q *rxq;

	rxq = fep->rx_queue[queue];

	// prepare buffers at pool
	bstgw_ethpool_increase(NULL, rxq->bd.ring_size);

	bdp = rxq->bd.base;
	for (i = 0; i < rxq->bd.ring_size; i++) {
		skb = netdev_alloc_skb(ndev, FEC_ENET_RX_FRSIZE);
		if (!skb) {
            EMSG("OOM");
			goto err_alloc;
        }

		if (fec_enet_new_rxbdp(ndev, bdp, skb)) {
			dev_kfree_skb(skb);
			goto err_alloc;
		}

		rxq->rx_skbuff[i] = skb;

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_RX_INT);
		}
		bdp->cbd_sc = cpu_to_fec16(BD_ENET_RX_EMPTY |
				((bdp == rxq->bd.last) ? BD_SC_WRAP : 0));

		bdp = fec_enet_get_nextdesc(bdp, &rxq->bd);
	}

	return 0;

 err_alloc:
	fec_enet_free_buffers(ndev);
	return -ENOMEM;
}

static int
fec_enet_alloc_txq_buffers(struct net_device *ndev, unsigned int queue)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;
	struct bufdesc  *bdp;
	struct fec_enet_priv_tx_q *txq;

	txq = fep->tx_queue[queue];

	// prepare buffers at pool
	bstgw_ethpool_increase(NULL, txq->bd.ring_size);

	bdp = txq->bd.base;
	for (i = 0; i < txq->bd.ring_size; i++) {
		txq->tx_bounce[i] = linux_kmalloc(
			FEC_ENET_TX_FRSIZE + BSTGW_CACHE_LINE, GFP_KERNEL);

		if (!txq->tx_bounce[i]) {
			txq->caligned_tx_bounce[i] = NULL;
			goto err_alloc;
		}
		txq->caligned_tx_bounce[i] = (unsigned char *)roundup2(
			(vaddr_t)txq->tx_bounce[i], BSTGW_CACHE_LINE);
		// still must be in allocation frame
		if(!(
			(txq->caligned_tx_bounce[i] >= txq->tx_bounce[i]) &&
			((txq->caligned_tx_bounce[i] + FEC_ENET_TX_FRSIZE)
			<= (txq->tx_bounce[i] + FEC_ENET_TX_FRSIZE + BSTGW_CACHE_LINE))
			))
			panic("allocation alginment problem");

		bdp->cbd_bufaddr = cpu_to_fec32(0);

		if (fep->bufdesc_ex) {
			struct bufdesc_ex *ebdp = (struct bufdesc_ex *)bdp;
			ebdp->cbd_esc = cpu_to_fec32(BD_ENET_TX_INT);
		}
		bdp->cbd_sc = cpu_to_fec16((bdp == txq->bd.last) ?
				BD_SC_WRAP : 0);
		bdp = fec_enet_get_nextdesc(bdp, &txq->bd);
	}

	return 0;

 err_alloc:
	fec_enet_free_buffers(ndev);
	return -ENOMEM;
}

static int fec_enet_alloc_buffers(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	unsigned int i;

	for (i = 0; i < fep->num_rx_queues; i++)
		if (fec_enet_alloc_rxq_buffers(ndev, i))
			return -ENOMEM;

	for (i = 0; i < fep->num_tx_queues; i++)
		if (fec_enet_alloc_txq_buffers(ndev, i))
			return -ENOMEM;
	return 0;
}

//static int
//fec_enet_open(struct net_device *ndev)

int fec_enet_open_sw(struct net_device *ndev) {
	struct fec_enet_private *fep = netdev_priv(ndev);
	/* TODO: does order matter here? */
	napi_enable(&fep->route_tx_napi);
	napi_enable(&fep->rx_napi);
	netif_tx_start_all_queues(ndev);
	return 0;
}

/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */

#define FEC_HASH_BITS	6		/* #bits in hash */
#define CRC32_POLY	0xEDB88320

// TODO: can we kick this one?
static void set_multicast_list(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	//struct netdev_hw_addr *ha;
	unsigned int /*i, bit, data, crc,*/ tmp;
	//unsigned char hash;
	unsigned int hash_high = 0, hash_low = 0;


	tmp = io_read32(fep->hwp + FEC_R_CNTRL);
	tmp &= ~0x8;
	io_write32(fep->hwp + FEC_R_CNTRL, tmp);



	io_write32(fep->hwp + FEC_GRP_HASH_TABLE_HIGH, hash_high);
	io_write32(fep->hwp + FEC_GRP_HASH_TABLE_LOW, hash_low);
}




/* Set a MAC change in hardware. */
static int
fec_set_mac_address(struct net_device *ndev, void *p)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct sockaddr *addr = p;

	//todo: ok?
	struct rte_ether_addr tmp = { 0 };

	if (addr) {
		memcpy(tmp.addr_bytes, addr->sa_data, RTE_ETHER_ADDR_LEN);
		if (!rte_is_valid_assigned_ether_addr(&tmp))
			return -EADDRNOTAVAIL;
		rte_ether_addr_copy(&tmp, &ndev->hwaddr);
	}


	io_write32((vaddr_t)fep->hwp + FEC_ADDR_LOW,
		ndev->hwaddr.addr_bytes[3] | (ndev->hwaddr.addr_bytes[2] << 8) |
		(ndev->hwaddr.addr_bytes[1] << 16) | (ndev->hwaddr.addr_bytes[0] << 24));
	io_write32((vaddr_t)fep->hwp + FEC_ADDR_HIGH,
		(ndev->hwaddr.addr_bytes[5] << 16) | (ndev->hwaddr.addr_bytes[4] << 24));
/*	writel(ndev->dev_addr[3] | (ndev->dev_addr[2] << 8) |
		(ndev->dev_addr[1] << 16) | (ndev->dev_addr[0] << 24),
		fep->hwp + FEC_ADDR_LOW);
	writel((ndev->dev_addr[5] << 16) | (ndev->dev_addr[4] << 24),
		fep->hwp + FEC_ADDR_HIGH);*/
	return 0;
}

// Note: only used by currently unused netdev_op
static inline void fec_enet_set_netdev_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct fec_enet_private *fep = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	netdev->features = features;

	// TODO: !! (all features)
	/* Receive checksum has been changed */
	if (changed & NETIF_F_RXCSUM) {
		if (features & NETIF_F_RXCSUM)
			fep->csum_flags |= FLAG_RX_CSUM_ENABLED;
		else
			fep->csum_flags &= ~FLAG_RX_CSUM_ENABLED;
	}
}

// NOTE: only need _0, bcs. only 1 queue pair
static const unsigned short offset_des_active_rxq[] = {
	FEC_R_DES_ACTIVE_0, FEC_R_DES_ACTIVE_1, FEC_R_DES_ACTIVE_2
};

static const unsigned short offset_des_active_txq[] = {
	FEC_X_DES_ACTIVE_0, FEC_X_DES_ACTIVE_1, FEC_X_DES_ACTIVE_2
};

 /*
  * XXX:  We need to clean up on failure exits here.
  *
  */
// TODO: both worlds I guess, need to split
static int fec_enet_init(struct net_device *ndev)
{
	struct fec_enet_private *fep = netdev_priv(ndev);
	struct bufdesc *cbd_base;
	dma_addr_t bd_dma;
	int bd_size;
	unsigned int i;
	unsigned dsize = fep->bufdesc_ex ? sizeof(struct bufdesc_ex) :
			sizeof(struct bufdesc);
	unsigned dsize_log2 = __fls(dsize);

	if(dsize != (1 << dsize_log2)) panic("Invalid(?) descriptor size");
	// TODO: we might have to check/adapt alignment
	fep->rx_align = 0xf;
	fep->tx_align = 0xf;

	fec_enet_alloc_queue(ndev); // also inits total_tx/rx_ring_size

	bd_size = (fep->total_tx_ring_size + fep->total_rx_ring_size) * dsize;

	/* Allocate memory for buffer descriptors. */
	cbd_base = bstgw_dma_alloc_coherent(bd_size, &bd_dma);
	if (!cbd_base) {
		return -ENOMEM;
	}

	memset(cbd_base, 0, bd_size);


	/* Get the Ethernet address */
	fec_get_mac(ndev);
	/* make sure MAC we just acquired is programmed into the hw */
	fec_set_mac_address(ndev, NULL);


	/* Set receive and transmit descriptor base. */
	// note: we currenlty only have 1 rxq
	for (i = 0; i < fep->num_rx_queues; i++) {
		struct fec_enet_priv_rx_q *rxq = fep->rx_queue[i];
		unsigned size = dsize * rxq->bd.ring_size;

		rxq->bd.qid = i;
		rxq->bd.base = cbd_base;
		rxq->bd.cur = cbd_base;
		rxq->bd.dma = bd_dma;
		rxq->bd.dsize = dsize;
		rxq->bd.dsize_log2 = dsize_log2;
		rxq->bd.reg_desc_active = fep->hwp + offset_des_active_rxq[i];
		bd_dma += size;
		cbd_base = (struct bufdesc *)(((void *)cbd_base) + size);
		rxq->bd.last = (struct bufdesc *)(((void *)cbd_base) - dsize);
	}


	// note: we currenlty only have 1 txq
	for (i = 0; i < fep->num_tx_queues; i++) {
		struct fec_enet_priv_tx_q *txq = fep->tx_queue[i];
		unsigned size = dsize * txq->bd.ring_size;

		txq->bd.qid = i;
		txq->bd.base = cbd_base;
		txq->bd.cur = cbd_base;
		txq->bd.dma = bd_dma;
		txq->bd.dsize = dsize;
		txq->bd.dsize_log2 = dsize_log2;
		txq->bd.reg_desc_active = fep->hwp + offset_des_active_txq[i];
		bd_dma += size;
		cbd_base = (struct bufdesc *)(((void *)cbd_base) + size);
		txq->bd.last = (struct bufdesc *)(((void *)cbd_base) - dsize);
	}


	/* The FEC Ethernet specific entries in the device structure */
//todo:	ndev->watchdog_timeo = TX_TIMEOUT;
//nw/kick:	ndev->netdev_ops = &fec_netdev_ops;

	//writel(FEC_RX_DISABLED_IMASK, fep->hwp + FEC_IMASK);
	io_write32(fep->hwp + FEC_IMASK, FEC_RX_DISABLED_IMASK);
	netif_napi_add(ndev, &fep->rx_napi, fec_enet_napi_q1, 64 /*NAPI_POLL_WEIGHT*/);
	netif_napi_add(ndev, &fep->route_tx_napi, fec_route_xmit_napi, 64 /*NAPI_POLL_WEIGHT*/);

	if (fep->quirks & FEC_QUIRK_HAS_VLAN)
		/* enable hw VLAN support */
		ndev->features |= NETIF_F_HW_VLAN_CTAG_RX;

	if (fep->quirks & FEC_QUIRK_HAS_CSUM) {
		/* enable hw accelerator */
		ndev->features |= (NETIF_F_IP_CSUM | NETIF_F_RXCSUM | NETIF_F_SG);
		fep->csum_flags |= FLAG_RX_CSUM_ENABLED;
	}


//	ndev->hw_features = ndev->features;

//	fec_restart(ndev); //Note: will be called by fec_enet_open()
	return 0;
}


// -> fec -> phy -> cable
static int fec_xmit_handler(struct ifnet *net_dev, bstgw_ethbuf_t **pkts /* *pkts[] */, unsigned int pktcount) {
#ifdef BSTGW_FEC_IMX_DEBUG
    DMSG("fec_xmit_handler( npkts:= %u ) got called", pktcount);
#endif

	int processed = 0;
	for (unsigned int i=0; i < pktcount; i++) {
		netdev_tx_t ret = fec_enet_start_xmit((struct sk_buff *)pkts[i], net_dev);
		if (ret == NETDEV_TX_OK) {
			processed++;
			continue; // free should be done on tx_complete handler
		} else if(ret == NETDEV_TX_BUSY) {
			// no free xmit buffers
			// todo: would be nice to make it possible to distinguish
			//       this outcome
#ifdef BSTGW_DBG_STATS
			full_bd_ring++;
#endif
			break;
		} else
			panic("FEC xmit error");
	}

#ifdef BSTGW_FEC_IMX_DEBUG
	DMSG("fec_xmit_handler processed: %d (of %u)", processed, pktcount);
#endif
	return processed;
}


static int fec_egr_ntfy_handler(struct ifnet *net_dev) {
	// if currently stopped, nothing to do (keep updated)
	// TODO: is this "unlikely" a good idea?
	if(unlikely( netif_tx_queue_stopped(net_dev->egress_queue) )) {
		return 0;
	}
	struct fec_enet_private *fep = netdev_priv(net_dev);
	bstgw_fec_schedule_xmit(fep);
	return 0;
}

static bool fec_setup_napi(struct napi_struct *n) {
	struct condvar *napi_cv;
	struct mutex *napi_mtx;

    napi_cv = calloc(1, sizeof(struct condvar));
	if (!napi_cv) return false;

    napi_mtx = calloc(1, sizeof(struct mutex));
    if (!napi_mtx) {
		free(napi_cv);
		return false;
    }
    condvar_init(napi_cv);
    mutex_init(napi_mtx);

	napi_instance_setup(n, napi_cv, napi_mtx);
	return true;
}

static bool
fec_setup_ingress_queue(struct fec_enet_private *fep) {
	memset(&fep->in_queue, 0, sizeof(pktqueue_t));
	/* 3xBURST per RX round; on NIC stop, at least 2 TXC rounds, i.e.
	 * 2 RX rounds --> 6xBURST; but on NIC stop, there is probably some leftover
	 * s.t. we need 6xBURST + a bit --> 7 */
	fep->in_queue.capacity = BSTGW_RX_BURST_SZ * 7;
	fep->in_queue.slock = SPINLOCK_UNLOCK;
	fep->in_queue.pkt = malloc(sizeof(bstgw_ethbuf_t *) * fep->in_queue.capacity);
	if (!fep->in_queue.pkt) return false;
	atomic_flag_clear(&fep->no_kick);
	return true;
}

// -> router
static int fec_rx_handler(struct ifnet *net_dev,
    bstgw_ethbuf_t **pkts /* *pkts[] */, unsigned int pktcount) {
    if(__predict_false( (net_dev->worker_count < 2) || !net_dev->workers[1] )) {
        EMSG("FEC's router worker is missing");
        return -1;
    }
    router_if_input(net_dev->workers[1], pkts, pktcount);
    return 0;
}

static int
fec_probe(const void *fdt __unused, struct device *dev, const void *data)
{
	EMSG("fec_probe() got called");
	const struct platform_device_id *fec_id_data;
	
	
	struct fec_enet_private *fep;
//nw	struct fec_platform_data *pdata;
	struct net_device *ndev;
	struct irq_desc *irq;
	int i, /*irq,*/ ret = 0;
	struct resource *r;
//use fdt;	const struct of_device_id *of_id;
	static int dev_id;
//use fdt;	struct device_node *np = pdev->dev.of_node, *phy_node;
//nw	struct gpio_desc *gd;

	/* Init network device */
	ndev = bstgw_alloc_etherdev(sizeof(struct fec_enet_private));
	if (!ndev)
		return -ENOMEM;
	
//	SET_NETDEV_DEV(ndev, &pdev->dev);

    //strncpy(ndev->name, dev->name, IF_NAMESIZE); // "ethernet@0x218800"
	// moved below

	ndev->trust_status = BSTGW_IF_EXTERNAL;

    ndev->rx_handler = fec_rx_handler ;
    ndev->xmit_handler = fec_xmit_handler;
	ndev->egress_ntfy_handler = fec_egr_ntfy_handler;

	/* setup board info structure */
	fep = netdev_priv(ndev);

	/* Instantiate the BstGW NAPI struct/s */
	if(!fec_setup_napi(&fep->rx_napi)) goto failed_napi_init;
	if(!fec_setup_napi(&fep->route_tx_napi)) goto failed_tx_napi_init;

	if(!fec_setup_ingress_queue(fep)) goto failed_pktq;

	fec_id_data = data;

	strncpy(ndev->name, fec_id_data->name, IF_NAMESIZE);

	// TODO: store fec_id_data somewhere
	fep->quirks = fec_id_data->driver_data;

	fep->netdev = ndev;
	fep->num_rx_queues = 1;
	fep->num_tx_queues = 1;

	fep->sfec_state.primary_st = SFEC_PRI_UNINIT;
	fep->sfec_state.sub_st = SFEC_SUB_NONE;
	fep->sfec_st_slock = SPINLOCK_UNLOCK;
	fep->sfec_state.rcntl = 0;
	fep->sfec_state.ecntl = 0;
	fep->sfec_state.rmii_mode = 0;

	/* default enable pause frame auto negotiation */
	// NOTE: pause_flag prob. only for NW relevant?
	if (fep->quirks & FEC_QUIRK_HAS_GBIT)
		fep->pause_flag |= FEC_PAUSE_FLAG_AUTONEG;

	/* Select default pin state */
//nw	pinctrl_pm_select_default_state(&pdev->dev);

	/* Get FEC's IOMEM region and IO_SEC map/get it */
	r = bstgw_platform_get_resource(dev, RESOURCE_MEM, 0);
	if (!r) panic("FEC memory resource missing");
	fep->hwp = bstgw_devm_ioremap_resource(r);
	if (!fep->hwp) {
		ret = -1; // PTR_ERR(fep->hwp)
		goto failed_ioremap;
	}

	fep->pdev = dev;
	fep->dev_id = dev_id++;

	//if ((of_machine_is_compatible("fsl,imx6q"))
	fep->quirks |= FEC_QUIRK_ERR006687;

//todo:	fep->ptp_clk_on = false;
//todo:	mutex_init(&fep->ptp_clk_mutex);

	// TODO: atm disabled
	fep->bufdesc_ex = fep->quirks & FEC_QUIRK_HAS_BUFDESC_EX;

//todo/nw	if (fep->bufdesc_ex)
//todo/nw		bstgw_fec_ptp_init(pdev);

	ret = fec_enet_init(ndev);
	if (ret)
		goto failed_init;


	/* Interrupt handler(s) */
	fep->handler.handle = fec_enet_interrupt;
	fep->handler.data = fep;
	if(dev->num_irqs != FEC_IRQ_NUM) {
		EMSG("num_irqs: %u; expected: %u", dev->num_irqs, FEC_IRQ_NUM);
		panic("Unexpected IRQ number");
	}

	for (i = 0; i < FEC_IRQ_NUM; i++) {
		irq = &dev->irqs[i].desc;
		if (!irq) {
			ret = -1;
			goto failed_irq;
		}

		// NW pass-on IRQ
		if (i == (FEC_IRQ_NUM-1)) {
			fep->irq_pass = irq;
			continue;
		}

		ret = irq_add(irq, ITRF_TRIGGER_LEVEL, &fep->handler);
		if (ret)
			goto failed_irq;
	
		ret = irq_enable(irq);
		if (ret) {
			irq_remove(irq);
			goto failed_irq;
		}

		fep->irq[i] = irq;
	}


/* // TODO: ?!
	ret = of_property_read_u32(np, "fsl,wakeup_irq", &irq);
	if (!ret && irq < FEC_IRQ_NUM)
		fep->wake_irq = fep->irq[irq];
	else
		fep->wake_irq = fep->irq[0];
*/

	/* Carrier starts down, phylib will bring it up */
	//todo: we have no notion of a carrier state atm.
//todo:	netif_carrier_off(ndev);

	//note: disabled atm
	if (fep->bufdesc_ex /*&& fep->ptp_clock*/)
		IMSG("registered PHC device %d", fep->dev_id);

	fep->rx_copybreak = COPYBREAK_DEFAULT;
//todo:	INIT_WORK(&fep->tx_timeout_work, fec_enet_timeout_work);

	// from enet_open()
	ret = fec_enet_alloc_buffers(ndev);
	if (ret) {
		goto failed_enet_alloc;
	}


	ret = ifnet_dev_register(ndev);
    if (ret)
		goto failed_register;


	fep->mmio_slock = SPINLOCK_UNLOCK;
	if (emu_add_region(r->address[0], r->size[0], fec_emu_check, ndev) != 0) {
        EMSG("Failed adding emulation region");
		goto failed_emu_reg;
    }


	/* TODO/Warning:
	 *	csu_init() must be called before, else it breaks;
	 * 	consider re-moving csu_init() from initcalls to early gic init;
	 */
	csu_set_csl(17, true); // protect ENET (NIC)
	csu_set_sa(8, true); // ENET has secure DMA

	fep->sfec_state.primary_st = SFEC_PRI_PROBED;


	return 0;

//	emu_remove_region(r->address[0], r->size[0], fec_emu_check, ndev);
failed_register:
	fec_enet_free_buffers(ndev);
failed_enet_alloc:
failed_emu_reg:
failed_irq:
	// TODO: irq_remove/disable?!
failed_init:
//nw/todo	bstgw_fec_ptp_stop(pdev);
//nw failed_reset:
//nw failed_regulator:
//nw	clk_disable_unprepare(fep->clk_ipg);
//nw failed_clk_ipg:
//nw/todo	fec_enet_clk_enable(ndev, false);
//nw/todo failed_clk:
	dev_id--;
failed_ioremap:
	fep->sfec_state.primary_st = SFEC_PRI_ERROR;
	free(fep->in_queue.pkt);
failed_pktq:
	//tx
	mutex_destroy(fep->route_tx_napi.wait_mtx); free(fep->route_tx_napi.wait_mtx);
	condvar_destroy(fep->route_tx_napi.wait_cv); free(fep->route_tx_napi.wait_cv);
failed_tx_napi_init:
	//rx
	mutex_destroy(fep->rx_napi.wait_mtx); free(fep->rx_napi.wait_mtx);
	condvar_destroy(fep->rx_napi.wait_cv); free(fep->rx_napi.wait_cv);
failed_napi_init:
	//todo: free_netdev(ndev);
	free(ndev->priv_data);
	free(ndev);


	return ret;
}

/**************************************************************/

static const struct dt_device_match fec_dt_ids[] = {
	{ .compatible = "fsl,bstgw-imx6q-fec", .data = (void *)&fec_devtype[BSTGW_IMX6Q_FEC], },
	{ 0 }
};

const struct dt_driver fec_dt_driver __dt_driver = {
	.name = "fec_bstgw",
	.match_table = fec_dt_ids,
	.probe = fec_probe,
};
