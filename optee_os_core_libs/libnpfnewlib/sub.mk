global-incdirs-y += .

srcs-y += atoi.c
srcs-y += ffs.c
srcs-y += inet_pton.c
srcs-y += stpncpy.c
srcs-y += strcasecmp.c
srcs-y += strtol.c

srcs-y += strpbrk.c
srcs-y += strtok_r.c
srcs-y += strsep.c

cflags-lib-y+=	-D__BEGIN_DECLS=
cppflags-lib-y+=	-D__END_DECLS=
