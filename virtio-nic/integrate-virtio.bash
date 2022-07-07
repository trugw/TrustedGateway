#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to optee_os source directory"
    exit 1
fi

OPTEE_OS="$1"

mkdir -p "${OPTEE_OS}/core/bstgw"
ln -s `pwd`/vnic "${OPTEE_OS}/core/bstgw/virtio-nic"
