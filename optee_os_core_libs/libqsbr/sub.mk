#global-incdirs-y += include

#incdirs-y += external/include

global-incdirs-y += .

srcs-y += ebr.c
srcs-y += gc.c

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes

cflags-lib-y+=	-DTZR_OPTEE
cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_PTA_OPTEE
cppflags-lib-y+=	-DTZR_PTA_OPTEE

cflags-lib-y+=	-DNDEBUG
cppflags-lib-y+=	-DNDEBUG

cflags-lib-y+=	-D__BEGIN_DECLS=
cflags-lib-y+=	-D__END_DECLS=

cflags-lib-y+=	-DENOMEM=\(12\)
cflags-lib-y+=	-DEINVAL=\(22\)
