#!/bin/bash

#TODO: generalize directory name to get rid of the wrong '64'
DEST_DIR=../external/arm-packets
mkdir ${DEST_DIR} && pushd ${DEST_DIR} || exit 1

# Fetch the arm libs
#LIBSSL=libssl1.1_1.1.1d-0+deb10u3_armhf.deb
#LIBSSL_DEV=libssl-dev_1.1.1d-0+deb10u3_armhf.deb

#old
#LIBSSL=libssl1.1_1.1.1d-0+deb10u5_armhf.deb
#LIBSSL_DEV=libssl-dev_1.1.1d-0+deb10u5_armhf.deb
#LIBPCAP=libpcap0.8_1.8.1-6_armhf.deb
#LIBPCAP_DEV=libpcap0.8-dev_1.8.1-6_armhf.deb


#new (01/07/22)
LIBSSL=libssl1.1_1.1.1n-0+deb10u3_armhf.deb
LIBSSL_DEV=libssl-dev_1.1.1n-0+deb10u3_armhf.deb
LIBPCAP=libpcap0.8_1.8.1-6+deb10u1_armhf.deb
LIBPCAP_DEV=libpcap0.8-dev_1.8.1-6+deb10u1_armhf.deb


#TODO: unfortunately, something is wrong with their certificate, but it's still better than http
wget --no-check-certificate https://ftp.de.debian.org/debian/pool/main/o/openssl/${LIBSSL} && \
wget --no-check-certificate https://ftp.de.debian.org/debian/pool/main/o/openssl/${LIBSSL_DEV} && \
wget --no-check-certificate https://ftp.de.debian.org/debian/pool/main/libp/libpcap/${LIBPCAP} && \
wget --no-check-certificate https://ftp.de.debian.org/debian/pool/main/libp/libpcap/${LIBPCAP_DEV} \
|| exit 1

# Unpack them
mkdir libpcap libssl
dpkg-deb -x ${LIBPCAP} libpcap && \
dpkg-deb -x ${LIBPCAP_DEV} libpcap && \
dpkg-deb -x ${LIBSSL} libssl && \
dpkg-deb -x ${LIBSSL_DEV} libssl \
|| exit 1

# Cleanup
rm ${LIBSSL} ${LIBSSL_DEV} ${LIBPCAP} ${LIBPCAP_DEV}

popd
