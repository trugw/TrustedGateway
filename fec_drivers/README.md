# FEC Subdrivers (NIC)
The current TrustedGateway prototype supports the FEC (Fast Ethernet Controller)
NIC of the Nitrogen6X board (i.MX6 Quad CPU) as the secure NIC.
TruGW splits the FEC NIC driver into a trusted subdriver running in the TrustZone
TEE and an untrusted subdriver running in the normal world Linux kernel.
TruGW provides a minimal trusted NIC driver framework which partially mimics the
APIs of the Linux driver framework and enables easier porting of the trusted
subdriver.
Beyond porting, an integration into the TruGW network and worker frameworks is
required.

This directory contains the trusted and untrusted subdriver.

### Trusted Subdriver
* `bstgw/`                        -- trusted subdriver
* `include/`                      -- trusted header files
* `integrate-fec.bash`            -- used on bootstrap to link the trusted subdriver into OP-TEE OS

### Untrusted Subdriver
* `nw_linux/`                     -- untrusted subdriver (based on Linux FEC driver)
* `nw-fec.patch`                  -- integrates untrusted subdriver into Linux build, plus minor changes
* `untrusted-fec-integrate.bash`  -- used on bootstrap to link the untrusted subdriver into the Linux kernel
