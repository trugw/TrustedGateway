#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to opee_os source directory as sole argument"
    exit 1
fi

OPTEE_OS="$1"

# prepare library directories
mkdir "${OPTEE_OS}/core/lib/bstgw"
mkdir "${OPTEE_OS}/core/lib/bstgw/lib"{nv,cdb,npf{,/net},lpm,thmap,qsbr,sljit,bpfjit,npfnewlib}

# symlink make files
ln -s `pwd`/libnv/sub.mk "${OPTEE_OS}/core/lib/bstgw/libnv/"
ln -s `pwd`/libcdb/sub.mk "${OPTEE_OS}/core/lib/bstgw/libcdb/"
ln -s `pwd`/libnpf/sub.mk "${OPTEE_OS}/core/lib/bstgw/libnpf/"

ln -s `pwd`/liblpm/sub.mk "${OPTEE_OS}/core/lib/bstgw/liblpm/"
ln -s `pwd`/libthmap/sub.mk "${OPTEE_OS}/core/lib/bstgw/libthmap/"
ln -s `pwd`/libqsbr/sub.mk "${OPTEE_OS}/core/lib/bstgw/libqsbr/"
ln -s `pwd`/libsljit/sub.mk "${OPTEE_OS}/core/lib/bstgw/libsljit/"
ln -s `pwd`/libbpfjit/sub.mk "${OPTEE_OS}/core/lib/bstgw/libbpfjit/"

ln -s `pwd`/libnpfnewlib/sub.mk "${OPTEE_OS}/core/lib/bstgw/libnpfnewlib/"

ln -s `pwd`/libdpdk "${OPTEE_OS}/core/lib/bstgw/"

# create symlinks
ln -s `pwd`/../external/nvlist/src/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/libnv/"
ln -s `pwd`/../external/libcdb/src/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/libcdb/"
ln -s `pwd`/../external/npf/src/libnpf/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/libnpf/"
ln -s `pwd`/../external/npf/src/kern/{npf.h,stand/npf_tzr_trimmed_stand.h} "${OPTEE_OS}/core/lib/bstgw/libnpf/net/"

ln -s `pwd`/../external/liblpm/src/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/liblpm/"
ln -s `pwd`/../external/thmap/src/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/libthmap/"
ln -s `pwd`/../external/libqsbr/src/{ebr,gc}.{c,h} "${OPTEE_OS}/core/lib/bstgw/libqsbr/"

ln -s `pwd`/../external/bpfjit/sljit/sljit_src/sljit{Lir.{c,h},NativeARM*.c,Utils.c,ExecAllocator.c,Config{,Internal}.h} "${OPTEE_OS}/core/lib/bstgw/libsljit/"

ln -s `pwd`/../external/bpfjit/src/*.{c,h} "${OPTEE_OS}/core/lib/bstgw/libbpfjit/"
ln -s `pwd`/../external/arm-packets/libpcap/usr/include "${OPTEE_OS}/core/lib/bstgw/libbpfjit/pcap-include"

ln -s `pwd`/../external/newlib/include "${OPTEE_OS}/core/lib/bstgw/libnpfnewlib/newlib"
ln -s `pwd`/../external/newlib/src/*.c "${OPTEE_OS}/core/lib/bstgw/libnpfnewlib/"

# symlink all files
ln -s `pwd`/libpthwrap "${OPTEE_OS}/core/lib/bstgw/libpthwrap"

# symlink other files
mkdir -p "${OPTEE_OS}/core/bstgw"
mkdir -p "${OPTEE_OS}/core/include/bstgw"
ln -s `pwd`/bstgw/* "${OPTEE_OS}/core/bstgw/"
ln -s `pwd`/include/* "${OPTEE_OS}/core/include/bstgw/"

# leftover (is it needed for ta headers, or do they call mkdir on their own?)
mkdir -p "${OPTEE_OS}/lib/libutee/include/bstgw"

# NPFKERN Firewall
mkdir -p "${OPTEE_OS}/core/bstgw/npf_firewall/"
ln -s `pwd`/npfkern/sub.mk "${OPTEE_OS}/core/bstgw/npf_firewall/"
ln -s `pwd`/../external/npf/src/kern/*.h "${OPTEE_OS}/core/bstgw/npf_firewall/"

ln -s `pwd`/../external/npf/src/kern/stand "${OPTEE_OS}/core/bstgw/npf_firewall/"
# TODO: more fine-grained if libnpf is fine with it
#mkdir -p "${OPTEE_OS}/core/bstgw/npf_firewall/stand/"
#ln -s `pwd`/../external/npf/src/kern/stand/{....} "${OPTEE_OS}/core/bstgw/npf_firewall/stand/"

# npfkern source files (based on standalone makefile)
KERN_SRCS=( \
npf.c \
npf_conf.c \
npf_ctl.c \
npf_handler.c \
npf_mbuf.c \
npf_bpf.c \
npf_params.c \
npf_ruleset.c \
npf_rproc.c \
npf_tableset.c \
npf_if.c \
npf_inet.c \
npf_conn.c \
npf_connkey.c \
npf_conndb.c \
npf_state.c \
npf_state_tcp.c \
npf_nat.c \
npf_portmap.c \
npf_alg.c \
npf_sendpkt.c \
npf_worker.c \
npf_ext_normalize.c \
npf_ext_rndblock.c \
npf_alg_icmp.c \
)
#npf_alg_pptp.c \

for c in ${KERN_SRCS[@]}
do
    ln -s "`pwd`/../external/npf/src/kern/$c" "${OPTEE_OS}/core/bstgw/npf_firewall/"
done
