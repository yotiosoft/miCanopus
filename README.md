# miCanopus

MikanOSのREADME → README.MikanOS.md

## 開発環境

- MacBook Air 2020 (Apple M1)
- C Compiler: Apple clang version 13.1.6

## 実行（自環境）

```
./osbook/devenv/run_qemu.sh ./edk2/Build/MikanLoaderX64/DEBUG_CLANGPDB/X64/Loader.efi
```

実行できない場合

```
export PATH=/opt/homebrew/opt/llvm/bin:$PATH
export PATH=/opt/homebrew/sbin:/opt/homebrew/opt/binutils/bin:$PATH
source edksetup.sh
```
