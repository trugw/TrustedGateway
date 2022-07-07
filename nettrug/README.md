# NetTrug
This directory contains the main files of NetTrug, TruGW's trusted core component.
NetTrug provides trusted firewalling, routing, workers, and NIC driver support.
In addition, this directory contains the untrusted helper service (router_helper)
which registers untrusted Linux threads on NetTrug for the trusted workers (see paper,
Section 5.1).

## Directory Structure
* host/     -- the untrusted helper service (router_helper)

* napi/     -- the trusted workers and notifier (mimicking the Linxu NAPI API)

* pta/      -- the main NetTrug code
