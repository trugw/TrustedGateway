#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to optee_os source directory"
    exit 1
fi

OPTEE_OS="$1"

mkdir -p "${OPTEE_OS}/core/drivers/bstgw"
mkdir -p "${OPTEE_OS}/core/include/drivers/bstgw"

ln -s `pwd`/bstgw/* "${OPTEE_OS}/core/drivers/bstgw/"
ln -s `pwd`/include/* "${OPTEE_OS}/core/include/drivers/bstgw/"
