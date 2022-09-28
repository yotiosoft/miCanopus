#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/BlockIo.h>
#include <Guid/FileInfo.h>

struct MemoryMap {
  UINTN buffer_size;
  VOID* buffer;
  UINTN map_size;
  UINTN map_key;
  UINTN descriptor_size;
  UINT32 descriptor_version;
};

typedef struct {
  UINT64 Size, FileSize, PhysicalSize;
  EFI_TIME CreateTime, LastAccessTime, ModificationTime;
  UINT64 Attribute;
  CHAR16 FileName[];
} EFI_FILE_INFO;

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
  if (map->buffer == NULL) {
    return EFI_BUFFER_TOO_SMALL;
  }

  map->map_size = map->buffer_size;
  return gBS->GetMemoryMap(
    &map->map_size,                       // メモリマップ書き込み用のメモリ領域の大きさ
    (EFI_MEMORY_DESCRIPTOR*)map->buffer,  // メモリマップ書き込み用のメモリ領域の先頭ポインタ
    &map->map_key,                        // メモリマップを識別するための値を書き込む変数
    &map->descriptor_size,                // メモリマップの個々の行を示すディスクリプタのバイト数
    &map->descriptor_version              // この値は使わない
  );
}

const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
  switch (type) {
    case EfiReservedMemoryType: return L"EfiReservedMemoryType";
    case EfiLoaderCode: return L"EfiLoaderCode";
    case EfiLoaderData: return L"EfiLoaderData";
    case EfiBootServicesCode: return L"EfiBootServicesCode";
    case EfiBootServicesData: return L"EfiBootServicesData";
    case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
    case EfiConventionalMemory: return L"EfiConventionalMemory";
    case EfiUnusableMemory: return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode: return L"EfiPalCode";
    case EfiPersistentMemory: return L"EfiPersistentMemory";
    case EfiMaxMemoryType: return L"EfiMaxMemoryType";
    default: return L"InvalidMemoryType";
  }
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
  CHAR8 buf[256];
  UINTN len;

  CHAR8* header = "Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
  len = AsciiStrLen(header);
  file->Write(file, &len, header);

  Print(L"map->buffer = %08lx, map->map_size = %08lx\n", map->buffer, map->map_size);

  EFI_PHYSICAL_ADDRESS iter;
  int i;
  for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
       iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
       iter += map->descriptor_size, i++) {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
    // メモリディスクリプタの内容を文字列に変換
    len = AsciiSPrint(
      buf, sizeof(buf),
      "%u, %x, %-ls, %08lx, %lx, %lx\n",
      i, desc->Type, GetMemoryTypeUnicode(desc->Type),
      desc->PhysicalStart, desc->NumberOfPages,
      desc->Attribute & 0xffffflu
    );
  }

  // ファイルに書き出し
  file->Write(file, &len, buf);

  return EFI_SUCCESS;
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

  gBS->OpenProtocol(
      image_handle,
      &gEfiLoadedImageProtocolGuid,
      (VOID**)&loaded_image,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  gBS->OpenProtocol(
      loaded_image->DeviceHandle,
      &gEfiSimpleFileSystemProtocolGuid,
      (VOID**)&fs,
      image_handle,
      NULL,
      EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

  fs->OpenVolume(fs, root);

  return EFI_SUCCESS;
}

// メモリマップの書き出し
void write_memmap(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL* root_dir) {
  // day02
  CHAR8 memmap_buf[4096 * 4];
  struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
  GetMemoryMap(&memmap);

  EFI_FILE_PROTOCOL* memmap_file;

  root_dir->Open(
    root_dir, &memmap_file, L"\\memmap",
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0
  );

  SaveMemoryMap(&memmap, memmap_file);
  memmap_file->Close(memmap_file);
}

// カーネル起動前にブートサービスを停止させる
void disable_boot_service(EFI_HANDLE image_handle) {
  CHAR8 memmap_buf[4096 * 4];
  struct MemoryMap memmap = {sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0};
  
  // ブートサービス停止
  EFI_STATUS status;
  status = gBS->ExitBootServices(image_handle, memmap.map_key);

  // 失敗したら再度メモリマップを取得して再実行
  if (EFI_ERROR(status)) {
    status = GetMemoryMap(&memmap);
    // それでも失敗したらエラー
    if (EFI_ERROR(status)) {
      Print(L"failed to get memory map: %r\n", status);
      while(1);
    }
    status = gBS->ExitBootServices(image_handle, memmap.map_key);
    if (EFI_ERROR(status)) {
      Print(L"Could not exit boot service: %r\n", status);
      while(1);
    }
  }
}

// カーネルの読み込み
EFI_PHYSICAL_ADDRESS load_kernel(EFI_FILE_PROTOCOL* root_dir) {
  EFI_FILE_PROTOCOL* kernel_file;
  root_dir->Open(root_dir, &kernel_file, L"\\kernel.elf", EFI_FILE_MODE_READ, 0);

  UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
  UINT8 file_info_buffer[file_info_size];
  kernel_file->GetInfo(kernel_file, &gEfiFileInfoGuid, &file_info_size, file_info_buffer);

  EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
  UINTN kernel_file_size = file_info->FileSize;

  EFI_PHYSICAL_ADDRESS kernel_base_addr = 0x100000;         // カーネルのベースアドレスは0x100000（ld.lldのオプションで指定）
  gBS->AllocatePages(
    AllocateAddress, EfiLoaderData,
    (kernel_file_size + 0xfff) / 0x1000,                    // allocateするページ数
    &kernel_base_addr
  );
  kernel_file->Read(kernel_file, &kernel_file_size, (VOID*)kernel_base_addr);   // ファイル全体の読み込み
  Print(L"Kernel: 0x%0lx (%lu bytes)\n", kernel_base_addr, kernel_file_size);

  return kernel_base_addr;
}

// カーネルを起動
void boot_kernel(EFI_PHYSICAL_ADDRESS kernel_base_address) {
  UINT64 entry_addr = *(UINT64*)(kernel_base_address + 24);   // EFIの仕様よりエントリポイントはオフセット+24バイトの位置から

  typedef void EntryPointType(void);
  EntryPointType* entry_point = (EntryPointType*)entry_addr;
  entry_point();
}

EFI_STATUS UefiMain(EFI_HANDLE        image_handle,
                   EFI_SYSTEM_TABLE  *system_table) {
  system_table->ConOut->OutputString(system_table->ConOut, L"Hello, world!\n");

  // ルートディレクトリを開く
  EFI_FILE_PROTOCOL* root_dir;
  OpenRootDir(image_handle, &root_dir);

  // メモリマップの書き出し
  write_memmap(image_handle, root_dir);

  // カーネルの読み込み
  EFI_PHYSICAL_ADDRESS kernel_base_addr = load_kernel(root_dir);

  // ブートサービス停止
  disable_boot_service(image_handle);

  // カーネルを起動
  boot_kernel(kernel_base_addr);

  Print(L"All done\n");

  while (1);
  return EFI_SUCCESS;
}
