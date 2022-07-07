#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass path to optee_os source directory"
    exit 1
fi

OPTEE_OS="$1"

SECLOAK_REPO="`pwd`/../external/secloak-kernel"

ln -s ${SECLOAK_REPO}/core/drivers/* "${OPTEE_OS}/core/drivers/"
ln -s ${SECLOAK_REPO}/core/kernel/* "${OPTEE_OS}/core/kernel/"

ln -s ${SECLOAK_REPO}/core/arch/arm/secloak "${OPTEE_OS}/core/arch/arm/"

ln -s ${SECLOAK_REPO}/core/include/errno.h "${OPTEE_OS}/core/include/"
ln -s ${SECLOAK_REPO}/core/include/drivers/* "${OPTEE_OS}/core/include/drivers/"

ln -s ${SECLOAK_REPO}/core/arch/arm/include/secloak "${OPTEE_OS}/core/arch/arm/include/"
