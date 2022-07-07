# Important: npf.h is libnpf one, net/npf.h is kern one
# Do NOT make bstgw/npf_firewall/ a global include directory to avoid collisions!
global-incdirs-y += .

#incdirs-y += ../kern/stand
#incdirs-y += ../../../libqsbr/src
#incdirs-y += ../../../thmap/src
#incdirs-y += ../../../liblpm/src
#TODO: pcap/bpf.h header is missing! (-> libpcap-devel) | Q: Is ARM vs. x86 header different?
#TODO: incdirs-y += ../../../bpfjit/src
srcs-y += npf.c

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes

incdirs-y += ../libnv/
libnames += nvlist
#libdirs += ../../../nvlist/out/tlib/

incdirs-y += ../libcdb/
libnames += cdb
#libdirs += ../../../libcdb/out/tlib/

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

cflags-lib-y+=	-DENOENT=\(2\)
cflags-lib-y+=	-DENOMEM=\(12\)
cflags-lib-y+=	-DEEXIST=\(17\)
cflags-lib-y+=	-DEINVAL=\(22\)
cflags-lib-y+=	-DENOTSUP=\(134\)

cflags-lib-y+=	-D__BEGIN_DECLS=
cflags-lib-y+=	-D__END_DECLS=
