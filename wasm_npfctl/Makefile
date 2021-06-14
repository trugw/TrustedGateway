#todo: call Makefiles of libcdb, nvlist, libnpf, then build npfctl

all:
	make -C libcdb/src
	make -C nvlist/src
	make -C npf/src/libnpf
	make -C npf/src/npfctl
	cd ./scripts; ./patch_proxy_client.bash; ./update_checksums.bash; cd ..

clean:
	make -C libcdb/src clean
	make -C nvlist/src clean
	make -C npf/src/libnpf clean
	make -C npf/src/npfctl clean

.PHONY: all clean
