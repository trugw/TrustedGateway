#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to opee_os source directory as sole argument"
    exit 1
fi

OPTEE_OS="$1"

mkdir -p "${OPTEE_OS}/lib/libutee/include/bstgw"

# cp source files
ln -s `pwd`/../external/bstgw_mbed_config.h "${OPTEE_OS}/lib/libmbedtls/include/"

