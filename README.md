# Trusted Gateway (TruGW)

This is the research prototype of the TrustedGateway (TruGW), a commodity gateway
architecture that leverages an ARM TrustZone-assisted TEE (OP-TEE) to isolate the
NIC I/O path and security critical network services---routing and firewall---from
vulnerable auxiliary services.


## Research Paper
The corresponding research paper "`TrustedGateway: TEE-Assisted Routing and Firewall Enforcement Using ARM TrustZone`" has been accepted for publication and is going to appear in the proceedings of the 25th International Symposium on Research in Attacks, Intrusions and Defenses ([RAID'22](https://raid2022.cs.ucy.ac.cy/)).
The *author's version* of the paper is *already available* in the [CISPA Publications Database](https://cispa.de/en/research/publications/3723-trustedgateway-tee-assisted-routing-and-firewall-enforcement-using-arm-trustzone) ([paper pdf](https://publications.cispa.saarland/3723/1/raid-trugw-sigconf.pdf), [preliminary bibtex](https://publications.cispa.saarland/cgi/export/eprint/3723/BibTeX/cispa-eprint-3723.bib)) for private use (not redistribution), and the definitive version of record will soon be available in the ACM Digital Library (https://dl.acm.org/).



## Deployment Warning
This is a *proof of concept* research prototype of TrustedGateway.
The prototype is *NOT* ready for secure use in production environments, e.g., due
to implementation simplifications such as the current use of hardcoded demo certificates in ConfigService (see configservice/README.md).
While all of these issues should be fixable, the prototype is currently in a freezed state.


## Directory Structure
* `configservice/`               -- contains the ConfigService TA and its untrusted helper service SockHelper; ConfigService provides a trusted, minimal mbedTLS HTTPS server and serves the trusted configuration web application; users are authenticated based on client certificates and the web app communicates via REST (GET/POST) APIs through ConfigService with the trusted NetTrug firewall/router;

* `dev-access-traps/`		-- contains script to integrate our adapted and extended SeCloak fork into TruGW/OP-TEE (for NIC isolation and memory/MMIO access traps/emulation)

* `eval/`                 -- contains evaluation instructions and scripts

* `external/`			    -- contains all kind of dependencies, including our extended SeCloak fork, NPF, OP-TEE, imx6 U-Boot, and the imx6 Linux kernel; gets patched by us as part of the boostrap process; NPF libraries are built as part of the patched TruGW/OP-TEE build process

* `fec_drivers/`			-- Linux untrusted FEC driver; TruGW/OP-TEE trusted FEC driver components; buffer APIs

* `nettrug/`			    -- TruGW's main component: NetTrug; it provides a trusted Ethernet/IP router implementation with trusted driver framework and napi-like workers and notifications for scheduling

* `optee_os_core_libs`    -- contains TruGW source files and Makefiles for integrating the NPF libs (cf. external/) and additional functionalities as core libraries into the OP-TEE OS

* `patches/` 			    -- contains *many* patches of TruGW to the NPF libraries and OP-TEE for porting and integrating NPF into TruGW/OP-TEE and for compiling and packaging the TruGW components as part of the OP-TEE build process

* `scripts/`			    -- contains scripts for getting OP-TEE/NPF symbol information and for fetching ARM32 libraries required to cross-compile npfctl (and later npfkern)

* `virtio-nic/`			-- the OP-TEE virtual NIC based on the VirtIO MMIO configuration specification

* `wasm_npfctl/`          -- contains trimmed NPF libraries and JS files for compiling `npfctl` as a Wasm module inside the admin/master browser; the compilation is done using `emscripten`; the web-npfctl files are expected to be hosted by a normal world web server and fetched by the secre admin/master web code (see READMEs in configservice/ and wasm_npfctl/);

*  `wip-install-dependencies.bash` -- more or less deprecated script that installs some of the dependencies required for compiling TruGW/OP-TEE; it provides hints and pointers, but you might have to additionally refer to the docs of the respective submodules for more information


## Requirements

### OS Versions and Target Platform
The prototype build process has been tested on an Ubuntu 18.04.6 LTS host system
with a 4.15.0-188 Linux kernel for cross-compilation to ARM 32 bit (armhf).
The TruGW target platform is the [Nitrogen6X board](https://boundarydevices.com/product/nitrogen6x/) with its FEC (Fast Ethernet Controller) NIC.
While most TEE code modules are platform-independent extensions to the OP-TEE OS,
there are platform-specific parts (e.g., NIC driver, device tree changes) tailored
to the Nitrogen6X board.

We assume that you have checked the dependencies listed in `wip-install-dependencies.bash`
and in the docs of the respective submodules (if required).

**Important:** We have used the **2GB version** of the Nitrogen6X board (rather than
1GB default version) for our prototype and configured the OP-TEE memory layout
and boot script configurations accordingly.
Use the 2GB version or tweak the respective boot configuration files and OP-TEE
memory layout respectively.

The Nitrogen6X features an NXP i.MX6 Quad CPU (ARM Cortex-A9, 32 bit).
In our test setup, we run Debian 10 with a 4.14 Linux kernel, OP-TEE 3.8.0,
and U-Boot 2018.07 on the board (see paper Section 7.1).
We use the imx6 versions provided by the board vendor Boundary Devices.
We build U-Boot and the Linux kernel from scratch as part of the TruGW bootstrap
process, but require a pre-installed Debian OS on the board.

Boundary Devices provides multiple imx Debian images for download with respective
install instructions, for instance here: https://boundarydevices.com/debian-buster-10-2-for-i-mx6-7-boards-december-2019-kernel-4-14-x/


### Board Preparation (Secure Memory Traps)
TruGW requires the hardware access protection of the TEE memory to be enabled.
On the Nitrogen6X board, this requires to explicitly enable the TZASC (TrustZone
Address Space Controller) protection by burning an eFuse.
A tutorial on how to burn eFuses on i.MX boards is given here: https://imxdev.gitlab.io/tutorial/Burning_eFuses_on_i.MX/

In our case, we had to execute the following command in the imx-u-boot menu after
board startup:
```
fuse prog 0 6 0x10000010
```
where bit 28 is the TZASC enable bit and the 4th bit enabled fuse-based configuration
(the 4th bit was pre-set in our board).
Explanations of eFuses can be looked up in the respective CPU/board data sheets,
reference manuals, or security documents.

Without this configuration there will be no hardware access protections such that
access attempts by the normal world to protected secure pages will not be prevented
and cannot generate a catchable access exception/fault, e.g., required for TruGW's
VNIC and NIC access emulation.



## Bootstrap and Build Process
The build process includes many steps and includes the compilation of U-Boot, the
Linux kernel, OP-TEE OS kernel, and some trusted and untrusted userspace applications.

Please refer to "Preparing Master Certificate for Compilation" in `configservice/README.md`
to configure the demo master admin certificate before compilation.

To build all components, perform the following steps:
```
# Bootstrap all dependencies, patch git subprojects, create symlinks, etc.
./bootstrap.bash imx

# Compile OP-TEE and TruGW components
pushd external/optee/build/
make
popd
```
The boostrap script should only be run _once_.
Afterwards, it is sufficient to call the `make` command when changes to the source
code have been made.
Please check the respective bootstrap file and Makfile on errors to debug single
steps, e.g., failing dependency downloads due to library updates (has happened
for libssl).

*Warning: Downloading the old ARM gcc compiler tool chains sometimes got stuck in past bootstrap attempts.*


## Installation Process and Run Instructions
The Nitrogen6X board stores its software on a NOR flash (SPI) and microSD card.
When updating TruGW components, most of the time, you will only copy new files
on the microSD card which include all the trusted TEE and untrusted OS components
(incl. device tree files).
Updating the U-Boot image (typically only required once) is more tricky as it requires
writing to the flash memory.

The installation steps assume that an imx Debian 10 image (ideally by Boundary
Devices) has already been installed on the Nitrogen6X board.



### A. Updating U-Boot on the Nitrogen6X board
U-Boot is stored on the NOR flash (SPI) of the Nitrogen6X board rather than on the
microSD card.
Our current protoype expects a certain imx6 U-Boot version to be installed on the
board.
We download and compile this U-Boot version (and a specific imx6 Linux kernel
version) as part of the bootstrap script.
To install the new U-Boot version on the board, please follow the board vendor
instructions for using their special update scripts (6x_upgrade/6q_upgrade).

See for example this blog post: https://boundarydevices.com/i-mx-6dq-u-boot-updates/

imx6 u-boot Github repo: https://github.com/boundarydevices/u-boot





### B. Installing or Updating TruGW's TEE Image and Services
Installing/Updating the TEE image requires copying the new TEE image
files into the /boot directory of the Nitrogen6X board (microSD card).
```
cd external/optee/build
sudo cp ../optee_os/out/arm/core/sImage <path-to-sdcard>/boot/sImage
```

For installing/updating the TruGW userspace services, copy and (re)install TruGW's
extended OP-TEE debian package:
```
# From external/optee/build
sudo cp ../out/optee_3.8.0-29-g9bfa7349-0.deb <path-to-sdcard>/home/debian/

# On the running board:
sudo apt remove optee # might be 'op-tee' instead
sudo dpkg -i /home/debian/optee_3.8.0-29-g9bfa7349-0.deb
```
*Warning*: exact name of `.deb` file depends on git commit



### C. Installing the new Boot Script
We provide an updated boot script for booting into TruGW/OP-TEE's secure kernel.
The script is provided in the `patches/imx6q` folder and is compiled as part of
the bootstrap process.
Copy the compiled bootstrap script to the boot partition of the Nitrogen6X board:
```
cp ./patches/imx6q/sp_bootscript.scr <path-to-sdcard>/boot/
```



### D. Installing the Linux kernel with DT changes and untrusted FEC subdriver
TruGW patches the FEC NIC driver to interact with the trusted subdriver running
in TrustZone.
In addition, there are some smaller tweaks and some device tree changes to make
the secure NIC operation and interrupt sharing between the normal and secure world
work.
The imx6 Linux kernel is downloaded, patched, and compiled as part of the bootstrap
script.
To recompile and/or install the kernel, follow the below instructions, adapted from
tutorials of the board vendor.
```
# Cross-compile the Linux kernel for ARM
cd external/optee/linux-boundary-imx_4.14.x_2.0.0_ga
bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export KERNEL_SRC=$PWD
export INSTALL_MOD_PATH=$KERNEL_SRC/ubuntunize/linux-staging

# Generate a file archive
make zImage modules dtbs -j8
make -C ubuntunize tarball
cd ubuntunize

# Copy to the microSD card
sudo cp linux-4.14.98-gcdeb032e6755-dirty.tar.gz <path-to-sdcard>/home/debian/
```
**Warning**: exact name of `.gz` depends on git commit and also affects the commands
below.

Unpack and install the kernel on the running Nitrogen6X board:
```
sudo apt-get purge linux-boundary-* linux-header-* linux-image-* # not necessarily needed
sudo tar --numeric-owner -xf /home/linux-4.14.98-gcdeb032e6755-dirty.tar.gz -C /
sudo update-initramfs -c -k4.14.98-gcdeb032e6755-dirty
sudo apt-get update && sudo apt-get dist-upgrade # not necessarily needed
sync && sudo reboot

cd /boot
mkimage -A arm -O linux -T ramdisk -C gzip -n "initram" -d initrd.img-4.14.98-gcdeb032e6755-dirty uramdisk.img
```

Now reboot the Nitrogen6X board (see next).





### E. Correctly Booting the Nitrogen6X Board with TruGW enabled
Communication with the Nitrogen6X board happens via a serial console interface
as described in blog articles of the board vendor: https://boundarydevices.com/just-getting-started/

During development, we have used a serial cable together with the screen utility
on Linux:
```
sudo screen /dev/ttyUSB0 115200
```

We have patched the boot script of the board, but still need to tweak the respective
boot commands a little bit to boot the board into our TruGW TEE image(s) rather
than into plain Linux without any TEE enabled:
```
# Enter the below commands in the u-boot console of the booting Nitrogen6X board
# (via serial console)

# Note: 2016m is only correct if you are using the 2GB version of the board.
# The 32 MB delta is due to the memory region reserved as TEE secure memory.
setenv extbootargs 'mem=2016m'

setenv loader 'load mmc 0:1
setenv sp_boot 'setenv bootdev /dev/mmcblk0p1; ${loader} 0x10008000 /boot/sp_bootscript.scr && source 10008000'
savee

# After saving the new configurations ('savee'), it is sufficient to run the following
# command on each reboot to boot with TrustZone + TruGW:
run sp_boot

# Just calling 'boot' causes a Linux boot without TruGW/OP-TEE enabled
```




### F. Post-boot Run Instructions
Default board credentials for imx Debian images by Bounday Devices:
```
debian:Boundary
```

After boot, you must start OP-TEE's supplicant service and afterwards the TruGW
helper service which registers normal world threads on the TruGW networking stacks
for scheduling trusted workers (see paper Section 5.1).
```
# Run TruGW worker helper in background (todo: convert into system service)
sudo su
tee-supplicant -d
optee_example_router_helper &
```

To allow for trusted remote policy configuration, TruGW's untrusted SockHelper
service must be started as well (see paper Section 4.3).
```
sudo su
optee_example_sock_helper
```

*Warning: In our setup, a Linux kernel bug let the urandom init hang for about 2min on boot. Therefore, SSL will not work until it has finished the setup. This affects apache2 (`service apache2 restart`) and for instance HTTPS connections via cURL. In addition it might affect connections to the trusted policy configuration web service.*


## Network Configuration
See evaluation README (`eval/README.md`) for hints regarding the IP subnetworks
of the client hosts.
Note that for vanilla (TruGW disabled, plain Linux), IP forwarding must be enabled
`echo 1 > /proc/sys/net/ipv4/ip_forward` as root.
Use a cooling fan for iPerf3 benchmarks to prevent overheating!
Before starting vanilla evaluation scripts, remember to call `eval/disable_fec_features.bash`
to turn off FEC features that are currently not yet supported by TruGW's trusted
NIC subdriver.



## Firewall Configuration
The firewall configuration syntax is that of NPF given here: https://rmind.github.io/npf/configuration.html

To configure rules on the FEC NIC, define rules '`on "imx6q-fec"`', where "in" rules
refer to traffic coming from external clients and "out" rules to traffic to external
clients.

To configure rules on TruGW's VNIC, define rules '`on NW`', where "in" rules refer
to traffic coming from untrusted services (Up), i.e., the normal world, and "out"
rules to traffic to the normal world.

Note that "imx6q-fec" and "NW" are defined in `fec_drivers/bstgw/imx_fec.c` and
`virtio-nic/vnic/src/vnic.c`.

The initial routing rules are currently hardcored in `nettrug/pta/config.c`.

See `eval/eval-rules/` for a set of sample firewall rules.



## Misc.
During testing, we have disabled sshd:
```
service sshd stop
```



## (Development/Testing) Updating only the Device Tree Blob
TruGW patches the device tree files of the Nitrogen6X board for enabling secure
NIC I/O and interrupt forwarding to the normal world NIC subdriver.
While the patched DT files will be installed with the patched kernel, it is possible
to manually update/modify the files:

```
cd ./patches/imx6q/device-trees/

# Use the TruGW-patched DT blob
sudo cp bstgw/imx6q-nitrogen6x.dtb <path-to-sdcard>/boot/imx6q-nitrogen6x.dtb

# Without TruGW patches (*warning: TruGW-enabled boot will probably hang*)
sudo cp vanilla/imx6q-nitrogen6x.dtb <path-to-sdcard>/boot/imx6q-nitrogen6x.dtb
```

*Warning: the device tree files might be outdated*



## License
The project is currently licensed under GNU General Public License v2.0 (GPLv2).
Note that many files have incomplete/missing copyright and/or licensing information
where the additional (or exclusive) GPLv2 licensing is not explicitly repeated,
but still applied.


## Limitations / Todos
Please note again that this is a *proof of concept* research prototype.

Also see limitations/todos in READMEs of subfolders.


* There are many outdated comments (esp. TODOs) flying around in the code that should be removed or updated.

* There are old names in the code, e.g., "bstgw" instead of "trugw".

* There are some leftovers from previous builds on the Hikey 960 board (from an old prototype version) that can be removed, esp. in the NPF makefiles.

* Build process integration can be improved, esp. regarding TruGW's TAs are build as part of the OP-TEE examples at the moment.

* VNIC's batch processing / notification coalescing regarding the normal world could be further improved.

* The VNIC node is currently not yet dynamically added to the device tree. Some stub code for doing so is already given in `arch/arm/kernel/generic_boot.c` in the `add_bstgw_dt_node()` function (after applying the TruGW/OP-TEE OS patch), but is currently nop'ed out (via 'return 0;' at the beginning). Instead, the VNIC node is statically added to the device tree as part of the kernel patch given in `patches/imx6q/extended-secloak-imx6_linux-changes.patch` at the moment, which is applied by the bootstrap process.

* In earlier versions, the Nitrogen6X board froze with disabled FEC/PHY interface
(`ifconfig set down`) when entering the powersave mode.
If this is still the case, a temporary workaround can be using the "performance"
governor of `cpufreq-set`.

* tba. *(this list is incomplete)*
