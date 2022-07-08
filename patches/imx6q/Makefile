sp_bootscript.scr: sp_bootscript.script
	mkimage -A arm -O linux -T script -C none -a 0 -e 0 -n "IMX Sec/NSec Boot Script" -d sp_bootscript.script sp_bootscript.scr

clean:
	rm *.scr
