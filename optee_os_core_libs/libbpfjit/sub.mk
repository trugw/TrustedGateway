global-incdirs-y += .

# TODO!
global-incdirs-y += pcap-include

srcs-y += bpfjit.c

cflags-lib-y+=	-DTZR_OPTEE
cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_PTA_OPTEE
cppflags-lib-y+=	-DTZR_PTA_OPTEE

cflags-lib-y+= -DSLJIT_CONFIG_AUTO=1
cppflags-lib-y+= -DSLJIT_CONFIG_AUTO=1

cflags-lib-y+= -DSLJIT_DEBUG=0
cppflags-lib-y+= -DSLJIT_DEBUG=0

cflags-lib-y+= -DSLJIT_VERBOSE=0
cppflags-lib-y+= -DSLJIT_VERBOSE=0
