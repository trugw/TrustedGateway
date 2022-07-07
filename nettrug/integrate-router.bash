#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to optee_os source directory"
    exit 1
fi

OPTEE_OS="$1"

mkdir -p "${OPTEE_OS}/core/bstgw"
mkdir -p "${OPTEE_OS}/lib/libutee/include/bstgw"

# TODO: fix path; avoid copy
#ln -s ../../../../core/bstgw/nettrug/pta_trustrouter.h "${OPTEE_OS}/lib/libutee/include/bstgw/"
ln -s `pwd`/pta/pta_trustrouter.h "${OPTEE_OS}/lib/libutee/include/bstgw/"

ln -s `pwd`/pta "${OPTEE_OS}/core/bstgw/nettrug"
ln -s `pwd`/napi "${OPTEE_OS}/core/bstgw/trust_napi"
