# ConfigService and SockHelper

## Introduction
This folder contains the secure `ConfigService` TA (trusted application) with
corresponding untrusted host part `SockHelper` (see paper, Figure 3).
At the moment, the directory gets symlinked by the bootstrap script into
external/optee/optee_examples/ for compilation (as "sock_helper/") as part of the
TruGW/OP-TEE build process.

The TA is a minimal HTTPS server which authenticates users via TLS client certificates,
ships a small trusted web bundle, and exposes special, access-guarded REST interfaces.

The `npfctl` WASM app (see wasm_npfctl/ in root directory) is not directly
shipped by ConfigService, but rather is fetched by the small trusted web bundle
using script tags and SRI (subresource integrity), as well as XHRs (together with CORS).
The WASM app is expected to be hosted by a normal world web server, e.g., Apache2.

Untrusted/Legacy web services might be integrated into the trusted ConfigService
page(s) via [sandboxed iframes](https://www.w3schools.com/tags/att_iframe_sandbox.asp).



## Directory Structure
* assets/               -- contains the small trusted web pages (HTML files) sent to the users

* external/             -- statless pico HTTP parser and custom mbedTLS file to-be-symlinked into OP-TEE

* externalAssets/       -- third party javascript files includes by the ConfigService page

* host/                 -- SockHelper: host part which does the TCP socket handling and HTML file reading

* patches/              -- patch for integrating our adapted mbedTLS config with enabled TLS stack into OP-TEE OS

* ta/                   -- ConfigService: TA which basically acts as minimal HTTPS server with REST API URLs for proxying messages between the trusted web app and the trusted router/firewall; performs access control based on client TLS certificates;


## Preparing Master Certificate for Compilation
In the current prototype, the certificate management of ConfigService is very simplified
(see "Limitations / Todos").
The certificate of the master admin is hardcoded in `ta/sw_crt_mng.c` rather than
being dynamically registered and loaded.

Please open `ta/sw_crt_mng.c` and replace the definition of `TEST_MASTER_CERT_PEM`
with your PEM-encoded TLS/RSA master admin certificate and `bstgw_test_master_crt_rsa_der`
with the DER-encoded version of it.

Afterwards, you can follow the instructions in the main README file for bootstrapping
and compiling TrustedGateway and all of its components.



## Configuration and Deployment
In the following we provide a brief step-by-step guide on how to configure Apache2 to set ConfigService with normal world Apache2 file hosting up.
Please refer to the Apache2 documentation for more details on configuring a normal world web server.

0. Follow the bootstrap, compilation, and installation instructions given in the main README file. The TruGW/OP-TEE client Debian package includes the ConfigService TA, SockHelper binary, and web resources (incl. WASM npfctl).

1. On the board, install apache2 and enable the required modules:
    ```
    sudo apt install apache2
    sudo service apache2 stop

    # for HTTPs + CORS header
    sudo a2enmod ssl
    sudo a2enmod headers
    ```

2. Apache2 configuration:
    ```
    pushd /etc/apache2/ 

    # change port to 8000
    sudo vim ports.conf

    # copy config
    sudo cp \
    /opt/bstgw/assets/apache2/bstgw.conf \
    sites-available/bstgw.conf

    # enable
    pushd sites-enabled
    #sudo rm ./*
    sudo ln -s ../sites-available/bstgw.conf .
    popd

    popd
    ```

3. HTTPS certificate:
    ```
    pushd /etc/apache2/

    # SSL config
    mkdir ssl

    # create demo.crt, demo.key (see Apache2 documentation)
    # openssl ...

    popd
    ```

4. Symlink JS/WASM files
    ```
    sudo ln -s \
    /opt/bstgw/assets/webapp-npfctl/ \
    /var/www/html/
    ```

5. Restart Apache2:
    ```
    sudo service apache2 restart
    ```
    You can check if the normal world files are correctly hosted by accessing
    `https://<ip>/webapp-npfctl/:8000`.



## Updating the ConfigService, SockHelper, and Web Resources
To update the binaries and web resources, you have to copy the new version of the
TruGW/OP-TEE debian package onto the board and replace the installed one with the
new version.
The instructions are also provided in B. of the main README.

1. Uninstall TruGW/OP-TEE client packet
    ```
    sudo apt remove op-tee
    ```

2. Push the new one from `external/optee/out/*.deb` to the device

3. Install the package on the device
    ```
    sudo dpkg -i ./optee_3.8.0-*.deb
    ```

## Limitations / Todos
* The client certificate of the master admin is currently hardcoded in `ta/sw_crt_mng.c`
    rather than being dynamically registered and loaded.

* Loading of the ConfigService main HTML files is not yet protected (see `ta/sw_fileio.c`).

* Instead of securely generating and storing a TLS server key pair and certificate for ConfigService
    we are currently just using the mbedTLS-embedded test certificate: `mbedtls_test_srv_crt` (see `ta/sw_mbedtls_util.c`).

* Why does webapp_external/ not yet contain our custom sjcl version as used for npfctl Wasm? (cf. wasm_npfctl/nw_offload/sjcl/sjcl.js, symlinked by wasm-npf)
