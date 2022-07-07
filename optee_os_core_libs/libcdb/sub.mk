global-incdirs-y += .

srcs-y += cdbr.c
srcs-y += cdbw.c
srcs-y += mi_vector_hash.c

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes
#cflags-lib-y+=	-DTZR_OPTEE
#cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_OPTEE
cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_PTA_OPTEE
cppflags-lib-y+=	-DTZR_PTA_OPTEE

cflags-lib-y+=	-D__RCSID\(x\)=
cppflags-lib-y+=	-D__RCSID\(x\)=

cflags-lib-y+=	-DNDEBUG
cppflags-lib-y+=	-DNDEBUG

cflags-lib-y+=	-D__BEGIN_DECLS=
cppflags-lib-y+=	-D__END_DECLS=
