# Custom SJCL Build
Custom compiled version of sjcl which includes the codec.bytes.toBits() API
which is not part of the standard hosted build.

See: https://github.com/bitwiseshiftleft/sjcl

The API is required by our `pre-js/` code for securely loading the Wasm worker
and symlinked into the wasm-npf source code for compilation.
