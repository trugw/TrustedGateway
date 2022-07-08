#!/bin/bash
DST_DIR="`pwd`/iperf/build/"

echo "Pulling iPerf3 module"
git submodule update --init ./iperf || exit 1

echo "Configuring and compiling into ${DST_DIR}"
pushd iperf && \
    ./configure --prefix=${DST_DIR} && \
    make && make install && \
popd || exit 1

echo -e "Done.\nSee ${DST_DIR}."
exit 0
