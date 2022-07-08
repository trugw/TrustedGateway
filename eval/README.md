# Evaluation
This directory contains instructions and scripts for measuring the throughput
performance of TruGW using iPerf3, as well as a set of sample firewall configurations.

See Section 7 of our paper for more details on the setup and additional measurements
performed as part of our prototype evaluation.


## Directory Structure
* `build_iperf.bash`          -- script for building iPerf3 from source (submodule)

* `disable_fec_features.bash` -- script for disabling FEC features unsupported by current TruGW drivers (for fairer comparison to vanilla setup)

* `eval-rules/`               -- contains sample firewall rules

* `iperf/`                    -- submodule for building iPerf3 (you can also use a prebuilt binary from your packet repo)

* `iperf-bench/`              -- script for the iPerf3 testing (see instructions below)


## <a name="setup"></a> Hardware Setup
In our setup, we use a LAN consisting of three system:  client systems `hostL` and `hostM`, plus the trusted `router` (TruGW).
They are interconnected using a [TL-SG105E](https://www.tp-link.com/de/business-networking/easy-smart-switch/tl-sg105e/) 5-Port Gigabit Easy Smart Switch of tp-link:
```
| Host L | <--Gbps--> |        |
                      |        |
| TrUGW  | <--Gbps--> | Switch |
                      |        |
| Host M | <--Gbps--> |        |
```
Host L and M route all their communication through the trusted router.


### Host Systems
We have used two different host systems to have a reasonable representation of a realistic setup:

`hostL` is an HP Z1 workstation 64 GB RAM, an Intel® Core™ i7-9700K CPU @ 3.60GHz × 8, and Intel I219-LM (rev 10) NIC.
The host is a `L`inux machine running Ubuntu 18.04.5 LTS with 4.15.0 Linux kernel.

`hostM` is a `M`acBook Pro (late 2013) with 8 GB RAM, an Intel i5 2,4GHz Dual-Core, and an Apple Thunderbolt-to-Gigabit Ethernet Adapter (A1433 EMC 2590).
The host runs macOS Big Sur version 11.2.3.


## Evaluation Setup

### Reference Measurements (vanilla)
The reference measurements use the same linux-imx6 version at the router, but with OP-TEE, incl. all TruGW extensions, disabled.
To have comparable conditions, we adjust the vanilla setup to better match the current feature set of our TruGW prototype.
As shown in `disable_fec_features.bash`, we perform the following standard adjustements:
* disable all forms of NIC offloading (e.g., checksum, tso/gso)
* disable optional tx/rx optimizations (e.g., scatter-gather I/O)



### Network Configurations
For the evaluation, `hostL` and `hostM` are separated using two IP subnetworks which are interconnected by the trusted `router`:
```
hostL: 172.16.0.7/24
hostM: 192.168.178.4/24

router:
(1) 172.16.0.49/24
(2) 192.168.178.49/24
```
The `router` is configured as the default gateway for both hosts, s.t. all traffic will eventually pass through our trusted router.

### Core pinning considerations
Core pinning has a major impact on the throughput performance.
In the current setup, there are 4 cores, and core 0 is dedicated to handle interrupts.
Therefore, the least performance-relevant thread should be pinned to core 0, while others should be pinned to distinct cores of 1-3.

Sample configuration:
```
core 0 --> notifier thread
core 1 --> VNIC worker
core 2 --> FEC rx worker
core 3 --> FEC tx worker
```
Having FEC at core 0 caused a loss of 50 Mbps in some of our tests.
Pinning of the notifier thread to core 1 instead of 0 caused performance variations of 35 Mbps and an overall loss of > 10 Mbps average throughput.

*Hint: The core affinity can be changed using the `taskset` command. In a future version, the router helper could automatically assign them.*

## iPerf3
[iPerf3](https://iperf.fr/) measures the throughput of a TCP/UDP connection. We have chosen version 3.9 for our evaluation (newest at point of writing).

### Preparation
Pull and build iPerf3 using the build script:
```
cd eval/
./build_iperf.bash
```
The default install directory is `build/` inside `eval/iperf/`.

### Running the Experiment
We assume the [previously](#setup) described setup.
One system will acts as the traffic sender (iPerf3 client) and one as the receiver (iPerf3 server).
Depending on which throughput should be measured, you can choose `hostL` and `hostM`, or one of the hosts and the trusted `router` as sender and receiver.

The results are stored by the iPerf3 server in a JSON format.
To not further stress the `router`, we currently store the measurement results always on the host-side.
Therefore, we only use the iPerf3 client on the router-side -- either in default mode for sending, or in _reverse_ mode for receiving (`recv-router.bash`).


On the server-side, run the following script to listen on \<srv-ip4\> for the iPerf3 client:
```
cd eval/iperf-bench/
./srv-side.bash <SRV-IP4>
```

Then, on the client-side, run the following script to start the benchmark, where the client will send to the server:
```
cd eval/iperf-bench/
./cli-side.bash <SRV-IP4>
```

When using the router as receiver, run the client in reverse mode on the router-side:
```
cd eval/iperf-bench/
./cli-side.bash -r <SRV-IP4>

# OR:
cd eval/iperf-bench/
./recv-router.bash <SRV-IP4>
```

By default, the benchmark measures the maxium throughput of a single TCP connection for 10 sec, 20 iterations each.

When you choose the trusted `router` as either the client or server, the throughput is measured between the untrusted world of the router (Linux) and the chosen host (A/B).

