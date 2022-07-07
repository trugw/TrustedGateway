# WARNING: do NOT make this a global include directory bcs. of collision with libnpf/npf.h

indirs-y += stand/
indirs-y += .

# TODO: does not seem to be a good idea and might cause bpf.h name collisions
global-incdirs-y += stand

#INCS=		npf.h npfkern.h

# TODO: isn't npf missing?
libnames += pthwrap nv qsbr thmap lpm cdb sljit bpfjit

# TODO !
#LDFLAGS+=	-ljemalloc

srcs-y += npf.c
srcs-y += npf_conf.c
srcs-y += npf_ctl.c
srcs-y += npf_handler.c
srcs-y += npf_mbuf.c
srcs-y += npf_bpf.c
srcs-y += npf_params.c
srcs-y += npf_ruleset.c
srcs-y += npf_rproc.c
srcs-y += npf_tableset.c
srcs-y += npf_if.c
srcs-y += npf_inet.c
srcs-y += npf_conn.c
srcs-y += npf_connkey.c
srcs-y += npf_conndb.c
srcs-y += npf_state.c
srcs-y += npf_state_tcp.c
srcs-y += npf_nat.c
srcs-y += npf_portmap.c
srcs-y += npf_alg.c
srcs-y += npf_sendpkt.c
srcs-y += npf_worker.c
srcs-y += npf_ext_normalize.c
srcs-y += npf_ext_rndblock.c
srcs-y += npf_alg_icmp.c
#srcs-y += npf_alg_pptp.c

srcs-y += stand/npfkern.c stand/bpf_filter.c
srcs-y += stand/murmurhash.c
srcs-y += stand/ebr_wrappers.c


cflags-lib-y+=	-DTZR_OPTEE
cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_PTA_OPTEE
cppflags-lib-y+=	-DTZR_PTA_OPTEE

cflags-lib-y+=	-DTZR_NO_FD_APIS
cppflags-lib-y+=	-DTZR_NO_FD_APIS

cflags-lib-y+=	-D__KERNEL_RCSID\(x,y\)=
cppflags-lib-y+=	-D__KERNEL_RCSID\(x,y\)=

cflags-lib-y+=	-D_NPF_STANDALONE
cppflags-lib-y+=	-D_NPF_STANDALONE

cflags-lib-y+=	-DNDEBUG
cppflags-lib-y+=	-DNDEBUG

cflags-lib-y+=	-D__BEGIN_DECLS=
cflags-lib-y+=	-D__END_DECLS=

cflags-lib-y+=	-D__END_DECLS=

cflags-lib-y+= -D__RCSID\(x\)=
cflags-lib-y+= -D_GNU_SOURCE -D_DEFAULT_SOURCE
cflags-lib-y+= -Wno-unused-local-typedefs -Wno-unused-parameter

cflags-lib-y+=	-DENOTSUP=\(134\)

# !! TODO
# WARNING: All symbols must be hidden by default to not conflict with
# the libnpf(3) library.  The debug version would, however, conflict.
#
#cflags-lib-y+=	-fvisibility=hidden
