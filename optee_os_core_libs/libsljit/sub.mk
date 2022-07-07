global-incdirs-y += .
srcs-y += sljitLir.c

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes

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


#cflags-sljitLir.c-y += -Wno-switch-default -Wno-unused-parameter
#cflags-sljitLir.c-y += -Wno-sign-compare

# we must not ignore those ones as they cause crashes in ARM!
#cflags-sljitLir.c-y += -Wno-cast-align

