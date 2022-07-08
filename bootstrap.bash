#!/bin/bash
OPTEE_DIR=`pwd`/external/optee
PATCH_DIR=`pwd`/patches

IMX_DIR="${PATCH_DIR}/imx6q"

echo "Starting Bastion Gateway bootstrap"

# Fetch submodules
echo "Fetching submodules"
git submodule init && git submodule update || exit 1


# Fetch OP-TEE
echo "Fetching OPTEE (this takes a while!)"
mkdir -p "${OPTEE_DIR}" && pushd "${OPTEE_DIR}" || exit 1


#NOTE/TODO: currently we have only forked optee_os, as we mainly touch this (plus build)
git clone https://github.com/trugw/optee_os.git -b 3.8.0 && \
git clone https://github.com/OP-TEE/optee_test.git -b 3.8.0 && \
git clone https://github.com/OP-TEE/optee_client.git -b 3.8.0 && \
git clone https://github.com/OP-TEE/build.git -b 3.8.0 && \
git clone https://github.com/linaro-swg/optee_examples.git -b 3.8.0 || exit 1

echo "Fetching imx repos (as archives)"
# forked from boundarydevices (they have renamed the repo to u-boot in the meantime)
wget https://github.com/trugw/u-boot-imx6/archive/boundary-v2018.07.zip && \
unzip ./boundary-v2018.07.zip && rm ./boundary-v2018.07.zip || exit 1

# forked from boundarydevices (they have renamed the repo to linux in the meantime)
wget https://github.com/trugw/linux-imx6/archive/boundary-imx_4.14.x_2.0.0_ga.zip && \
unzip ./boundary-imx_4.14.x_2.0.0_ga.zip && rm ./boundary-imx_4.14.x_2.0.0_ga.zip || exit 1

UBOOT_DIR="${OPTEE_DIR}/u-boot-imx6-boundary-v2018.07"
LINUX_DIR="${OPTEE_DIR}/linux-imx6-boundary-imx_4.14.x_2.0.0_ga"

# init a local git repo to better track changes
pushd "${LINUX_DIR}"
git init && git add . && git commit -m "initial checkout" || exit 1
popd

#git clone https://github.com/boundarydevices/u-boot-imx6.git -b boundary-v2018.07 && \
#git clone https://github.com/boundarydevices/linux-imx6.git -b boundary-imx_4.14.x_2.0.0_ga || exit 1
#    git clone https://github.com/boundarydevices/imx_usb_loader.git || exit 1

popd || exit 1


# Fetch ARM packets for linking
echo "Downloading ARM/32 Debian (Buster) packets"
pushd scripts/
./fetch-arm32-debian-buster-deps.bash || exit 1
popd


# Patch the NPF libraries
echo "Patching NPF libraries"
pushd external/

## bpfjit and sljit
SLJIT_TAR="sljit-r313.tar"
svn co https://svn.code.sf.net/p/sljit/code@r313 sljit && \
tar cf ${SLJIT_TAR} sljit/ || exit 1
pushd bpfjit/
patch -p1 < "${PATCH_DIR}/npf/optee-bpfjit.patch" || exit 1
tar kxf ../${SLJIT_TAR}
rm ../${SLJIT_TAR}
pushd sljit/sljit_src
patch < "${PATCH_DIR}/npf/sljit_src-changes.patch" || exit 1
popd
popd

## libcdb
pushd libcdb/
patch -p1 < "${PATCH_DIR}/npf/optee-cdb.patch" || exit 1
popd

## liblpm
pushd liblpm/
patch -p1 < "${PATCH_DIR}/npf/optee-liblpm.patch" || exit 1
popd

## libqsbr
pushd libqsbr/
patch -p1 < "${PATCH_DIR}/npf/optee-libqsbr.patch" || exit 1
popd

## npf
pushd npf/
# 2nd patch will have offset of 1 line
patch -p1 < "${PATCH_DIR}/npf/optee-npfr.patch" && \
patch -p1 < "${PATCH_DIR}/npf/optee-npf.patch" || exit 1
popd

## nvlist
pushd nvlist/
patch -p1 < "${PATCH_DIR}/npf/optee-nvlist.patch" || exit 1
popd

## thmap
pushd thmap/
patch -p1 < "${PATCH_DIR}/npf/optee-thmap.patch" || exit 1
popd

popd


# Patch OPTEE OS
echo "Patching OPTEE OS and integrating files into it"
pushd patches/optee_os/
./integrate-patches.bash "${OPTEE_DIR}/optee_os"
popd


# Patch OPTEE build directory
echo "Patching OPTEE build directory"
pushd external/optee/build/
patch -p1 < "${PATCH_DIR}/my-optee-debian-build.patch" || exit 1
ln -s imx-nitrogen6q.mk Makefile || exit 1
popd


# Patch OPTEE examples
echo "Integrating TAs into OPTEE examples"
pushd "${OPTEE_DIR}/optee_examples"
patch -p1 < "${PATCH_DIR}/optee-examples-to-master.patch" || exit 1
popd

## TODO: direct integration into OPTEE folder + build
ln -s `pwd`/nettrug "${OPTEE_DIR}/optee_examples/router_helper" && \
ln -s `pwd`/configservice "${OPTEE_DIR}/optee_examples/sock_helper" || exit 1


# Integrate into OPTEE root folder for build integration (TODO!)
echo "Integrating other TAs and libs into OPTEE's build"

## TODO: cleanup where possible
ln -s `pwd`/external/bpfjit "${OPTEE_DIR}/" && \
ln -s `pwd`/external/libcdb "${OPTEE_DIR}/" && \
ln -s `pwd`/external/liblpm "${OPTEE_DIR}/" && \
ln -s `pwd`/external/libqsbr "${OPTEE_DIR}/" && \
ln -s `pwd`/external/npf "${OPTEE_DIR}/" && \
ln -s `pwd`/external/nvlist "${OPTEE_DIR}/" && \
ln -s `pwd`/external/thmap "${OPTEE_DIR}/" || exit 1


# Patch NPF libraries required for the npfctl WASM module
echo "Preparing WASM Module"
pushd wasm_npfctl/

pushd libcdb/
patch -p1 < ../patches/libcdb.patch || exit 1
popd

pushd npf/
patch -p1 < ../patches/npf.patch || exit 1
popd

pushd nvlist/
patch -p1 < ../patches/nvlist.patch || exit 1
popd

echo "Compiling WASM Module"
emmake make || exit 1

popd

# Symlink for WebApp integration to OP-TEE build (TODO: symlink might not be required?)
ln -s `pwd`/wasm_npfctl "${OPTEE_DIR}/wasm_npfctl"


# Patch OP-TEE test
echo "Warning: optee_test/ had localization bugs regarding the 32-bit detection in our case"
echo "See patches/optee_test.patch"

pushd "${OPTEE_DIR}/optee_test"
patch -p1 < "${PATCH_DIR}/optee_test.patch" || exit 1
popd

# Compile U-Boot bootscript
echo "Compile imx6q u-boot script"
pushd "${IMX_DIR}"
make || exit 1
popd

# Config + Compile imx6 U-Boot
echo "Compile U-Boot for imx6q2g"
echo "Warning:  we recommend the board/boundary/nitrogen6x/6x_upgrade.txt \
    upgrade file instead of the board/boundary/bootscripts/upgrade.txt one"
pushd "${UBOOT_DIR}"
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
make nitrogen6q2g_defconfig && \
make -j2 || exit 1
popd

# Config + Compile imx6 Linux
echo "Prepare Linux for imx6"
# TODO: shrink config to boundary_defconfig + our changes (CAAM=n, virtio MMIO/NET=y, some disables of IRQ stuff)
# replace with our config
cp "${IMX_DIR}/our_long_kconfig" "${LINUX_DIR}/.config" || exit 1

pushd fec_drivers/
./untrusted-fec-integrate.bash "${LINUX_DIR}/"
popd

echo "Compile Linux for imx6"
echo "cf. ubuntunize/ for result tar file"
pushd "${LINUX_DIR}"
export KERNEL_SRC=$PWD
export INSTALL_MOD_PATH=$KERNEL_SRC/ubuntunize/linux-staging
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
# add our adapted SeCloak patches
patch -p1 < "${IMX_DIR}/extended-secloak-imx6_linux-changes.patch" || exit 1

#make boundary_defconfig \
make zImage modules dtbs -j`nproc` && \
make -C ubuntunize tarball || exit 1

echo "Install on board for instance the following way:"
echo "sudo apt-get purge linux-boundary-* linux-header-* linux-image-*"
echo "sudo tar --numeric-owner -xf /home/linux-4.14.98-gb6e7a699373e-dirty.tar.gz -C /"
echo "sudo update-initramfs -c -k4.14.98-gb6e7a699373e-dirty"
echo "sudo apt-get update && sudo apt-get dist-upgrade"
echo "cd /boot; mkimage -A arm -O linux -T ramdisk -C gzip -n \"initram\" -d initrd.img-4.14.98-gb6e7a699373e-dirty uramdisk.img"
popd


# Fetch OPTEE cross-compilation toolchains
echo "Last Step:  Fetching OPTEE's cross-compilation toolchains (can take few minutes)"
echo "Note:  The downloads sometimes seems to hang/crash causing the tar to exit with errors"
pushd "${OPTEE_DIR}/build"
make -f toolchain.mk aarch32 || exit 1
# 64bit:
#    make toolchains -j2 || exit 1
popd


echo "Bootstrap Successful!"

echo "To build TruGW, now go to ./external/optee/build/ and call 'make'."
exit 0
