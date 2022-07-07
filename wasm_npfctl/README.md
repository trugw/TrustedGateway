#  Trusted Configuration Wasm Application
This folder contains the trusted npfctl Wasm (WebAssembly) application.



## Introduction
TrustedGateway includes a customized version of NPF's npfctl configuration tool
as part of ConfigService's trusted web application.
During the build process, the required NPF libraries are trimmed to a minimum and
patched to become compilable with `emscripten` to Wasm.
Furthermore, they are extended to communicate with ConfigService's REST interfaces
for submitting the trusted firewall policies to TruGW.

The NPF libraries get patched with the files in `patches/` as part of the bootstrap process.
They are applicable to the same commits as the ones in the root patches/ folder.



## Directory Structure
* nw_offload/			-- Assets folder which contains symlinks to the generated worker and Wasm files, a custom build of SJCL which contains codec.bytes.fromBits() required for the hash-based integrity checks, and sample apache configuration files for the NW.

	Note: the HTML, JS and Wasm flies, as well as the demo apache2 config files are packed by OP-TEE into the *OP-TEE client Debian package*. The packet installs the files to `/opt/bstgw/assets/`.

* external/ 			-- 2 headers files required for the compilation which are not present in emscripten; borrowed from OP-TEE and slightly adapted (originally from newlib)

* libcdb, npf, nvlist 	-- the NPF libraries required for npfctl; they get patched on bootstrap using the patches in `patches/` before compilation

* patches/ 				-- patch files for the NPF libraries which trim them to the minimum required for npfctl, adapt them for the emscripten/Wasm compilation, and add support for interacting with ConfigService's REST APIs

* pre-js/				-- JS file prepended to the worker JS file by emscripten in which we setup a custom proxy channel (postMessage-based) between the trusted web page bundle and the npfctl Wasm module through the worker; the other channel endpoint is in the trusted HTML files (cf. `configservice/assets/`) and main JS file; the JS also includes the loading + hashing of the Wasm module

* proxy-js/				-- JS file integrated into the npfctl.js file during the compilation step. Is responsible for loading, checking, and starting the worker JS in a web worker.

* scripts/ 				-- Helper scripts for updating the SRI/hashing checksums and for intergating our proxy-js JS file for secure web worker setup




## Overview
The build generates a JS file for the main thread (`npfctl.js`), a JS file which will run inside a
web worker (`npfctl.worker.js`) and a Wasm file with the actual npfctl logic (`npfctl.wasm`).
During the build process, the 2 scripts in `scripts/` are used to further patch the generated npfctl.js file and update the sha256 and sha384 checksums in the JS files and SRI statements in the WebServer's HTML files (`configservice/assets/`).

The 3 files are expected to be hosted by a web server of the untrusted NW to save
memory in the SW and lower the TCB size/complexity.
The main JS file (`npfctl.js`) should be fetched via SRI by the main HTML file (cf. `configservice/assets/`).
The main JS file fetches the worker JS file and performs a sha256-based integrity check on it. Afterwards, it runs the code in a web worker. In an analogous manner, the worker JS file loads and sha256 checks the Wasm file.

We use the sha256 implementation of the `SJCL` JS crypto library. It is currently prepended to the
worker JS file (in a minimized form) during compilation and fetched by the trusted HTML files via SRI for the main JS.

Note that ConfigService and the normal world-hosted web resources must be from different origins.
In the current prototype, the ConfigService webapp assumes the NW server (Apache2) to use the same IP, but a different port. *(Note: they also use different certificates)*
Due to the different origins, the NW web server must use CORS (cross-origin resource sharing) headers to expliclity allow the ConfigService webapp context to fetch the JS and Wasm files.

ConfigService's webapp requires some third party JS files, e.g., jQuery and SJCL (see configservice/externalAssets/).
As TruGW's restrictive boot firewall policy (see paper, Section 5.4) denies access to external networks (incl. the internet), we must fetch these files also from the NW web server.
Their correctness is checked using SRI (subresource integrity).


## Compilation Steps
The compilation is done as part of the bootstrap process and when compiling TruGW/OP-TEE.
You _need not_ execute the following steps.
They are just for reference.

1. fetch the NPF libary submodules
2. patch the NPF libraries (`patch -p1 < ../patches/<lib>.patch`)
3. from root of wasm directory run: `emmake make`

_Note:_ Browsers perform aggressive caching of files. You might need to clear the
cache or use a new anonymous browser session to see the new file versions / get
rid of stale disk-cached versions in the browser.



## Network URLs and Website Fetches
ConfigService's web server runs under https://\<ip\>:`4433` (SW), while the normal world Apache2 webserver runs under https://\<ip\>:`8000` (NW).

ConfigService hosts the user type-specific (e.g., admin) HTML files ("base file").
The base file loads some external scripts via static \<script\> tags and SubResource Integrity,
namely jQuery, bootstrap, Popper, and sjcl (crypt).

The npfctl.js file is loaded via dynamically crafted \<script\> tag and SubResource Integrity
from: \<NW\>`/webapp-npfctl/npfctl.js`

npfctl.js uses a XHR to fetch the worker.js file from: \<NW\>`/webapp-npfctl/npfctl.worker.js`

and checks its integrity via `SHA256` using the SJCL library.
On success, the JS content is transformed into a Blob and started as Web Worker.

worker.js uses a XHR to fetch the npfctl.wasm Wasm file from: \<NW\>`/webapp-npfctl/npfctl.wasm`

and performs the same SHA256 checking mechanism as before.


## Hosting Considerations
The 2 XHRs issued by `npfctl.js` and `worker.js` are *cross-origin requests* from
the SW (ConfigService) domain to the NW (Apache2) domain and are therefore affected by the SOP.
In order to make the responses readable, the NW web server has to be configured to
set CORS headers which allow the SW origin to access the files.

_Note:_ alternatively the SINGLE_FILE mode of emscripten could be used to embed
worker.js and npfctl.wasm as base64 strings into npfctl.js, such that only that file
would have to be fetched via a script tag and SRI protection.


## Dynamic URLs
At the moment, the NW (Apache2) web server address is dynamically derived from the current domain by replacing the port with `8000`.

For the POST XHRs issued by the Wasm module (npfctl) to the SW, we have to use absolute
URLs, because the base URL of a blob worker is \<SW\>/\<blob_id\> which makes relative URLs
fail. The main JS passes the base URL to the Wasm module using a JS-exposed (emscripten) function. The Wasm module will use `<base>/trustconf/` as destinations for its POST requests.

Note that we run the Wasm module in a web worker which requires a post message-based
communication (proxy) between the Brower's main JS thread and the worker thread.
A post message system is auto-generated by emscripten, but we had to implement our own
custom handlers and messages on top of it (e.g., for calling into the Wasm module).


## Apache Configuration
See the sample configs in `nw_offload/apache2/`.
Note that you currently need an SSL key + certificate in `/etc/apache2/ssl/`.
You have to enable the `headers` and `ssl` module to be able to set the CORS headers
and use HTTPS.
The `a2enmod` command can be used to enable apache modules.

*Note: also cf. `configservice/README.md` for a step-by-step configuration.*


## Hash-based Integrity
Be careful when changing the JS code for the hash calculations, because it was hard to
get it right for the Wasm file.
Note that we used a self-compiled version of jscl, because the web-hosted
one does not include codec.bytes.toBits() which we need for the Wasm file.

The hash values are updated as part of the compilation step. The update procedure can be manually triggered from `scripts/` by running `./update_checksums.bash` inside it.

The CLI commands for manual checksum calculations are:
* `openssl dgst -sha384 -binary npfctl.js | openssl base64 -A`
* `openssl dgst -sha256 ./npfctl.worker.js`
* `openssl dgst -sha256 ./npfctl.wasm`


## Trusting the Server Certificates
_Note:_ we currently had to explicitly trust the certificate of the NW (Apache2)
in our local keychain service to make the connection work.


## Todos
* cleanup generated emscripten JS files, because we don't need most of the included functionality
* note that we currently manually perform the Wasm loading on our own, because otherwise
	the NW server could affect the loading mechanism based on its return values, e.g.,
	if Wasm MIME type is correctly set, a stream-based Wasm init is done (which we cannot
	integrity check); but otherwise, a fall-back to XHR-based fetching is done (which is
	similar to how we do it manually atm).
