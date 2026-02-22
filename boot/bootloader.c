/*
 * VaultOS UEFI Bootloader
 *
 * Compiled as a PE32+ EFI application using GNU-EFI or mingw cross-compiler.
 * Loads the kernel binary from ESP, sets up framebuffer (GOP), obtains the
 * UEFI memory map, and transfers control to kernel_main().
 *
 * Build: x86_64-w64-mingw32-gcc or via GNU-EFI toolchain
 */

#include <efi.h>
#include <efilib.h>

/* Same struct as include/vaultos/boot_info.h but with EFI types for alignment */
#define BOOT_INFO_MAGIC 0x5641554C544F5321ULL

typedef struct {
    UINT64 magic;
    UINT64 fb_base;
    UINT32 fb_width;
    UINT32 fb_height;
    UINT32 fb_pitch;
    UINT32 fb_bpp;
    UINT32 fb_pixel_format;
    UINT32 _pad0;
    UINT64 mmap_base;
    UINT64 mmap_size;
    UINT64 mmap_desc_size;
    UINT32 mmap_desc_version;
    UINT32 mmap_entry_count;
    UINT64 kernel_phys_base;
    UINT64 kernel_size;
    UINT64 runtime_services;
    UINT64 rsdp_address;
} __attribute__((packed)) BootInfo;

#define KERNEL_FILENAME L"\\VAULTOS.BIN"
#define KERNEL_LOAD_ADDR 0x100000  /* 1 MiB */

static EFI_STATUS init_gop(EFI_SYSTEM_TABLE *ST, BootInfo *info) {
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;

    status = uefi_call_wrapper(ST->BootServices->LocateProtocol, 3,
                                &gopGuid, NULL, (void **)&gop);
    if (EFI_ERROR(status)) {
        Print(L"GOP not found!\r\n");
        return status;
    }

    /* Try to find 1024x768 or use current mode */
    UINTN bestMode = gop->Mode->Mode;
    for (UINTN i = 0; i < gop->Mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *modeInfo;
        UINTN infoSize;
        status = uefi_call_wrapper(gop->QueryMode, 4, gop, i, &infoSize, &modeInfo);
        if (EFI_ERROR(status)) continue;

        if (modeInfo->HorizontalResolution == 1024 &&
            modeInfo->VerticalResolution == 768) {
            bestMode = i;
            break;
        }
    }

    status = uefi_call_wrapper(gop->SetMode, 2, gop, bestMode);
    if (EFI_ERROR(status)) {
        Print(L"Failed to set GOP mode\r\n");
    }

    info->fb_base = gop->Mode->FrameBufferBase;
    info->fb_width = gop->Mode->Info->HorizontalResolution;
    info->fb_height = gop->Mode->Info->VerticalResolution;
    info->fb_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    info->fb_bpp = 32;
    info->fb_pixel_format = (gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) ? 0 : 1;

    Print(L"GOP: %ux%u, FB at 0x%lx\r\n",
          info->fb_width, info->fb_height, info->fb_base);

    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST, BootInfo *info) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loadedImage;
    EFI_GUID lipGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE_PROTOCOL *root, *kernelFile;

    /* Get loaded image protocol */
    status = uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
                                ImageHandle, &lipGuid, (void **)&loadedImage);
    if (EFI_ERROR(status)) return status;

    /* Open filesystem */
    status = uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
                                loadedImage->DeviceHandle, &fsGuid, (void **)&fs);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) return status;

    /* Open kernel file */
    status = uefi_call_wrapper(root->Open, 5, root, &kernelFile,
                                KERNEL_FILENAME, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        Print(L"Cannot open %s\r\n", KERNEL_FILENAME);
        return status;
    }

    /* Get file size - EFI_FILE_INFO has variable-length FileName at end */
    UINT8 fileInfoBuf[sizeof(EFI_FILE_INFO) + 256];
    UINTN infoSize = sizeof(fileInfoBuf);
    EFI_GUID fiGuid = EFI_FILE_INFO_ID;
    status = uefi_call_wrapper(kernelFile->GetInfo, 4,
                                kernelFile, &fiGuid, &infoSize, fileInfoBuf);
    if (EFI_ERROR(status)) return status;

    EFI_FILE_INFO *fileInfo = (EFI_FILE_INFO *)fileInfoBuf;
    UINTN kernelSize = fileInfo->FileSize;

    /* Allocate memory for kernel at known physical address */
    EFI_PHYSICAL_ADDRESS kernelAddr = KERNEL_LOAD_ADDR;
    UINTN pages = (kernelSize + 4095) / 4096;
    if (pages < 512) pages = 512;  /* Reserve at least 2 MiB for BSS + stack */
    status = uefi_call_wrapper(ST->BootServices->AllocatePages, 4,
                                AllocateAddress, EfiLoaderData, pages, &kernelAddr);
    if (EFI_ERROR(status)) {
        /* Try allocating anywhere */
        status = uefi_call_wrapper(ST->BootServices->AllocatePages, 4,
                                    AllocateAnyPages, EfiLoaderData, pages, &kernelAddr);
        if (EFI_ERROR(status)) return status;
    }

    /* Read kernel into memory */
    status = uefi_call_wrapper(kernelFile->Read, 3, kernelFile, &kernelSize, (void *)kernelAddr);
    if (EFI_ERROR(status)) return status;

    uefi_call_wrapper(kernelFile->Close, 1, kernelFile);
    uefi_call_wrapper(root->Close, 1, root);

    info->kernel_phys_base = kernelAddr;
    info->kernel_size = kernelSize;

    Print(L"Kernel loaded: %lu bytes at 0x%lx\r\n", kernelSize, kernelAddr);

    return EFI_SUCCESS;
}

static EFI_STATUS find_rsdp(EFI_SYSTEM_TABLE *ST, BootInfo *info) {
    EFI_GUID acpi2Guid = ACPI_20_TABLE_GUID;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        if (CompareGuid(&ST->ConfigurationTable[i].VendorGuid, &acpi2Guid) == 0) {
            info->rsdp_address = (UINT64)ST->ConfigurationTable[i].VendorTable;
            return EFI_SUCCESS;
        }
    }
    info->rsdp_address = 0;
    return EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;

    InitializeLib(ImageHandle, SystemTable);

    Print(L"\r\n=== VaultOS Bootloader ===\r\n\r\n");

    /* Allocate BootInfo struct */
    static BootInfo bootInfo;
    bootInfo.magic = BOOT_INFO_MAGIC;

    /* Initialize GOP (framebuffer) */
    status = init_gop(SystemTable, &bootInfo);
    if (EFI_ERROR(status)) {
        Print(L"WARNING: No GOP framebuffer\r\n");
    }

    /* Load kernel */
    status = load_kernel(ImageHandle, SystemTable, &bootInfo);
    if (EFI_ERROR(status)) {
        Print(L"FATAL: Failed to load kernel! Status: %r\r\n", status);
        return status;
    }

    /* Find RSDP */
    find_rsdp(SystemTable, &bootInfo);

    /* Save runtime services pointer */
    bootInfo.runtime_services = (UINT64)SystemTable->RuntimeServices;

    /* Get memory map and exit boot services */
    UINTN mmapSize = 0, mapKey, descSize;
    UINT32 descVersion;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;

    /* First call to get required size */
    status = uefi_call_wrapper(SystemTable->BootServices->GetMemoryMap, 5,
                                &mmapSize, mmap, &mapKey, &descSize, &descVersion);

    mmapSize += 2 * descSize; /* Extra space for the allocation itself */

    status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3,
                                EfiLoaderData, mmapSize, (void **)&mmap);
    if (EFI_ERROR(status)) {
        Print(L"FATAL: Cannot allocate memory map buffer\r\n");
        return status;
    }

    /* Retry loop for GetMemoryMap + ExitBootServices */
    for (int retry = 0; retry < 3; retry++) {
        status = uefi_call_wrapper(SystemTable->BootServices->GetMemoryMap, 5,
                                    &mmapSize, mmap, &mapKey, &descSize, &descVersion);
        if (EFI_ERROR(status)) continue;

        bootInfo.mmap_base = (UINT64)mmap;
        bootInfo.mmap_size = mmapSize;
        bootInfo.mmap_desc_size = descSize;
        bootInfo.mmap_desc_version = descVersion;
        bootInfo.mmap_entry_count = mmapSize / descSize;

        status = uefi_call_wrapper(SystemTable->BootServices->ExitBootServices, 2,
                                    ImageHandle, mapKey);
        if (!EFI_ERROR(status)) break;
    }

    if (EFI_ERROR(status)) {
        /* Can't print anymore - boot services are gone (or we failed) */
        while (1) __asm__ volatile ("hlt");
    }

    /* Boot services are gone - no more Print() calls from here */

    /* Jump to kernel_main(BootInfo *) */
    typedef void (*kernel_entry_t)(BootInfo *);
    kernel_entry_t kernel_main = (kernel_entry_t)bootInfo.kernel_phys_base;
    kernel_main(&bootInfo);

    /* Should never reach here */
    while (1) __asm__ volatile ("hlt");
    return EFI_SUCCESS;
}
