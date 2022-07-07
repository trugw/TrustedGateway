srcs-y += jit_pool.c
subdirs-y += nettrug
srcs-y += worker_storage.c

# TODO: currently as lib in core.mk
#subdirs-y += npf_firewall

subdirs-$(CFG_BSTGW_VNIC) += virtio-nic

# TODO: experimentally only
global-incdirs-y += .

subdirs-y += trust_napi

srcs-$(CFG_BSTGW_SW_NIC) += dma_pool.c
srcs-$(CFG_BSTGW_CNTR) += bstgw_counter.c
