# Device Access Traps
TruGW prevents access attempts by the untrusted world to the NIC and emulates
such access attempts if being part of the NIC initalization by the untrusted subdriver.
In addition, TruGW uses memory access traps to detect accesses to the virtual device
registers of VNIC by the untrusted world.

We have ported the CSU configuration and the MMIO (memory-mapped I/O)
access trap and emulation code of the SeCloak research prototype to OP-TEE 3.8.0
(multi-core) and extended it for supporting NIC isolation, NIC interrupt sharing,
and virtual device registers for TruGW's VNIC.

Our changes are available as a fork of SeCloak on Github: https://github.com/trugw/secloak-kernel