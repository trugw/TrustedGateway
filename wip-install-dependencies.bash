#!/bin/bash

# Warning: Some of the below dependencies might not be required anymore to
#   build TrustedGateway. The list might be incomplete, but provides a starting
#   point. Please have a look at the dependency lists of the respective subprojects
#   if some of the bootstrap/build steps fail.

# OP-TEE Dependencies (see: https://optee.readthedocs.io/en/latest/building/prerequisites.html)
echo "OP-TEE package dependencies"
sudo dpkg --add-architecture i386
sudo apt-get update

sudo apt-get install android-tools-adb android-tools-fastboot autoconf \
        automake bc bison build-essential ccache cscope curl device-tree-compiler \
        expect flex ftp-upload gdisk iasl libattr1-dev libc6:i386 libcap-dev \
        libfdt-dev libftdi-dev libglib2.0-dev libhidapi-dev libncurses5-dev \
        libpixman-1-dev libssl-dev libstdc++6:i386 libtool libz1:i386 make \
        mtools netcat python-crypto python3-crypto python-pyelftools \
        python3-pycryptodome python3-pyelftools python-serial python3-serial \
        rsync unzip uuid-dev xdg-utils xterm xz-utils zlib1g-dev

# **WARNING:** You have to check what you need
#python -m pip install --user pycryptodome
python -m pip install --user pycryptodomex
#python3 -m pip install --user pycryptodome
python3 -m pip install --user pycryptodomex


# Android Repo Tool (for fetching OP-TEE repos) -- TODO: might not be the way we fetch OP-TEE anymore
echo "Android Repo (for OP-TEE)"
mkdir -p ~/bin
PATH=~/bin:$PATH

curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo

gpg --recv-key 8BB9AD793E8E6153AF0F9A4416530D5E920F5C65
curl https://storage.googleapis.com/git-repo-downloads/repo.asc | gpg --verify - ~/bin/repo


# Emscripten (WASM)
pushd external/
git clone https://github.com/emscripten-core/emsdk.git

pushd emsdk/
git pull
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
popd

popd


# mkimage
sudo apt install u-boot-tools

# cross-toolchain for u-boot + linux
sudo apt install device-tree-compiler
sudo apt install crossbuild-essential-armhf
sudo apt install gcc-arm-linux-gnueabihf
