#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to linux kernel source directory"
    exit 1
fi

LINUX_KERN="$1"

mkdir -p "${LINUX_KERN}/drivers/net/ethernet/freescale/bstgw"
ln -s `pwd`/nw_linux/* "${LINUX_KERN}/drivers/net/ethernet/freescale/bstgw/"

PATCH="`pwd`/nw-fec.patch"

pushd "${LINUX_KERN}"
patch -p1 < ${PATCH}
popd
