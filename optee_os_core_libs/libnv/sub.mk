global-incdirs-y += .

srcs-y += cnvlist.c
srcs-y += dnvlist.c
srcs-y += nvlist.c
srcs-y += nvpair.c

# To remove a certain compiler flag, add a line like this
#cflags-template_ta.c-y += -Wno-strict-prototypes

#cflags-lib-y +=
#cppflags-lib-y +=

cflags-lib-y+=	-DTZR_OPTEE
cppflags-lib-y+=	-DTZR_OPTEE

cflags-lib-y+=	-DTZR_PTA_OPTEE
cppflags-lib-y+=	-DTZR_PTA_OPTEE

cflags-lib-y+=	-D__FBSDID\(x\)=
cppflags-lib-y+=	-D__FBSDID\(x\)=
cflags-lib-y+=	-DNDEBUG
cppflags-lib-y+=	-DNDEBUG

## disable msgio (socket apis)
cflags-lib-y+=	-UWITH_MSGIO
cppflags-lib-y+=	-UWITH_MSGIO

## disable fprintf() error logs
cflags-lib-y+=	-UHAVE_PJDLOG
cppflags-lib-y+=	-UHAVE_PJDLOG

cflags-lib-y+=	-DENOMEM=\(12\)

cflags-lib-y+=	-DEEXIST=\(17\)

cflags-lib-y+=	-D__BEGIN_DECLS=
cppflags-lib-y+=	-D__END_DECLS=
