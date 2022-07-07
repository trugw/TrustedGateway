# Additional OP-TEE Core Libraries of TruGW
This directory contains additional source files of NetTrug (e.g., DMA/JIT memory pools)
and files for integrating the patched NPF libaries as core libraries into the TruGW/OP-TEE
build process.
The bootstrap process uses the provided script (`integrate-npf-core-libs.bash`)
to integrate the files into the OP-TEE OS directories via symlinks.
