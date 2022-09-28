cd ../edk2
source edksetup.sh
build
../osbook/devenv/run_qemu.sh Build/MikanLoaderX64/DEBUG_CLANGPDB/X64/Loader.efi
cd ../miCanopus
