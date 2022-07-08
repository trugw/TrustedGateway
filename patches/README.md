## **(Deprecated)** OP-TEE Patch for Debian-Linux (HiKey 960)

* Install the OP-TEE build prerequisites as specified in the OP-TEE docs, <https://optee.readthedocs.io/en/latest/building/prerequisites.html#prerequisites>

    CAUTION: I had problems installing pycrptodome, because there are 2 versions with
    sligthly different module naming scheme. I had to install `pycryptodomex` instead.
    ```
    python2/3 -m pip install --user pycryptodomex
    ```

* Install the AOSP repo tool:
	```
	mkdir ~/bin
	PATH=~/bin:$PATH
	curl https://storage.googleapis.com/git-repo-downloads/repo-1 > ~/bin/repo
	chmod a+x ~/bin/repo
	```

* Download the OP-TEE HiKey 960 repo version 3.8:
	```
	repo init -u https://github.com/OP-TEE/manifest.git -m hikey960.xml -b 3.8.0
	repo sync -j4 --no-clone-bundle
	```

* Apply main patch to build/ directory and adapt Makefile pointer (commit `aaabf8e3ac837c437b0b3b8f2dca7df87c899a74`):
	```
	cd build/
	patch -p1 < my-optee-debian-build.patch
	rm Makefile
	ln -s hikey960_debian.mk Makefile
	```

* Apply small patch which adds recent fixes to the examples:
	```
	cd optee_examples/
	patch -p1 < optee-examples-to-master.patch
	```

	Note: applicable to `559b2141c16bf0f57ccd72f60e4deb84fc2a05b0`, adds few patches to sync repo upto commit `f7f5a3ad2e8601bf2f846992d0b10aae3e3e5530`

* Get the cross-compiler toolchain:
	```
	cd build/
	make -j2 toolchains
	```

* Build everything:
	```
	make -j `nproc`
	```

* Install Android SDK "Platform-Tools" / fastboot (cf. `https://developer.android.com/studio/releases/platform-tools`)
	In our setup `apt search fastboot` showed the following:
	```
	android-tools-fastboot/xenial,now 5.1.1r36+git20160322-0ubuntu3 amd64  [installiert]
	```

* Install `screen` for connecting to UART console:
	```
	# install
	sudo apt install screen
	# connect to UART of HiKey 960 board
	sudo screen /dev/ttyUSB0 115200
	```

* Flush bootloader, Debian and co:
	```
	make flush
	```

* Configure Wifi/WLAN on the board, e.g., via `nmtui` to get an IP address

* Push OP-TEE package to device:
	```
	# the default passwd for root is "linaro"
	IP=<hikey-ip> make send
	```

	Note: the default password for user "linaro" is `linaro`.
	You can test it for instance via `ssh linaro@<hike-ip>`.

* Install packets on device:
	```
	dpkg --force-all -i /tmp/out/*.deb"
	dpkg --force-all -i /tmp/linux-image-*.deb" 
	```
	Note: Check if all relevant packets got copied! Otherwise copy missing ones and install!

	In our setup `apt search optee` showed the following:
	```
	op-tee/now 3.8.0-0 arm64 [installed,local]
	linux-headers-5.1.0-optee-rpb-253104-g9823b258/now 5.1.0-optee-rpb-253104-g9823b258-10 arm64 [installed,local]
	linux-image-5.1.0-optee-rpb-253104-g9823b258/now 5.1.0-optee-rpb-253104-g9823b258-10 arm64 [installed,local]
	```

	Note: We copied and installed the linux-headers and linux-libc-dev packages manually as they were missing.
	```
	linux-libc-dev/now 5.1.0-optee-rpb-253104-g9823b258-10 arm64 [installed,local]
	```


* Update Debian 9 to 10 (Buster), e.g., following the instrutions at <https://www.cyberciti.biz/faq/update-upgrade-debian-9-to-debian-10-buster/>


* Run tests:
	```
	# use screen to log into HiKey 960 board
	tee-supplicant -d
	xtest
	```

	All should pass. 


## Patching libtool

The `libtool_bin__xenial_2.4.6-0.1.patch` patch file slightly adapts the libtool-bin to only define the CC variable to `gcc` if CC is not yet defined.
This allows us to easily overwrite it with our cross-compiler for compiling the NPF libraries for arm64.
Apply via:
```
sudo patch -d /usr/bin/ < libtool_bin__xenial_2.4.6-0.1.patch
```
