# Nitrogen6X-specific Patches / Scripts
This folder contains multiple patches, configuration files, and/or scripts required for
running TruGW on the Nitrogen6X board.
They are mostly used by the bootstrap process, but the compiled boot script
must be copied manually to the boot partition of the board (as described in the
main README).

## Directory Structure
* device-trees/                             -- patched and unpatched device tree files of the Nitrogen6X board (mainly for reference)

* extended-secloak-imx6_linux-changes.patch -- SeCloak's Linux kernel patch for adding CSU registers to the device tree files, extended by TruGW to add a VNIC node and prepare the FEC (NIC) node for compatibility with the trusted and untrusted subdrivers

* gdb-init                                  -- commands used for debugging the Nitrogen6X board via gdb + JTAG hardware debugger

* Makefile                                  -- for compiling the custom boot script

* sp_bootscript.script                      -- custom boot script (based on SeCloak's boot script)



## Just for Reference: UBoot Commands (adapted from SeCloak)
*Note: see instructions in main README file instead*
```
setenv extbootargs 'mem=992m' # (1GB)
setenv extbootargs 'mem=2016m' # (2GB)
setenv loader 'load mmc 0:1'
setenv sp_boot 'setenv bootdev /dev/mmcblk0p1; ${loader} 0x10008000 /boot/sp_bootscript.scr && source 10008000
savee

run sp_boot
```

## Just for Reference: initramdisk for U-Boot:
`mkimage -A arm -O linux -T ramdisk -C gzip -n "initram" -d initrd.img-4.14.98-gb6e7a699373e uramdisk.img`

