global-incdirs-y += .

srcs-y += trust_router.c
srcs-y += if_netdev.c

srcs-y += bstgw_eth_buf.c

# from/based on NPF router
srcs-y += npf_router.c
srcs-y += route.c

srcs-y += worker.c

srcs-y += if_bstgw.c
srcs-y += npf_bstgw_ops.c

srcs-y += config.c

srcs-y += arp.c
