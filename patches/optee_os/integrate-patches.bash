#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to opee_os source directory as sole argument"
    exit 1
fi

# Pass absolute path to optee_os/ directory to make other scripts work!
if [[ "$1" != /* ]]
then
    echo "Error: Path to optee_os/ has to be an absolute path!"
    exit 1
fi

OPTEE_OS="$1"

PATCH=`pwd`/bstgw-full.patch
pushd "$1"
patch -p1 < $PATCH
popd

# Core libraries, memory pools
pushd ../../optee_os_core_libs
./integrate-npf-core-libs.bash "$1"
popd

# mbedTLS configurations
pushd ../../configservice/patches
./integrate-mbedtls-changes.bash "$1"
popd

# NetTrug
pushd ../../nettrug
./integrate-router.bash "$1"
popd

# virtio interface
pushd ../../virtio-nic
./integrate-virtio.bash "$1"
popd

# SeCloak-based device access traps (NIC and VNIC)
pushd ../../dev-access-traps
./integrate-secloak.bash "$1"
popd

# FEC drivers
pushd ../../fec_drivers
./integrate-fec.bash "$1"
popd
