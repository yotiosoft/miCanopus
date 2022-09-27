all: main.efi

%.o: %.c
	clang -target x86_64-pc-win32-coff -o $@ -c $<

%.efi: %.o
	lld-link /subsystem:efi_application /entry:EfiMain /out:$@ $<

.PHONY: run
run: main.efi
	../../devenv/run_qemu.sh main.efi

clean:
	rm -f main.efi
	rm -f disk.img
	rm -rf mnt
