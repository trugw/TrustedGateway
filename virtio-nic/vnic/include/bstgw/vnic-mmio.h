#ifndef BSTGW_VNIC_MMIO_H
#define BSTGW_VNIC_MMIO_H

#ifdef CFG_BSTGW_VNIC_IRQ
#define BSTGW_VNIC_IRQ (CFG_BSTGW_VNIC_IRQ)
#else
#define BSTGW_VNIC_IRQ (-1)
#error "vNIC enabled, but no IRQ defined. Please define CFG_BSTGW_VNIC_IRQ."
#endif

//#define BSTGW_VNIC_DBG_STATS

#ifdef BSTGW_VNIC_DBG_STATS
extern unsigned long int full_nw_recv;
#endif

#endif /* BSTGW_VNIC_MMIO_H */
