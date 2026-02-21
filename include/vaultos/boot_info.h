#ifndef VAULTOS_BOOT_INFO_H
#define VAULTOS_BOOT_INFO_H

#include "../../kernel/lib/types.h"

#define BOOT_INFO_MAGIC 0x5641554C544F5321ULL  /* "VAULTOS!" */

/* UEFI memory types we care about */
#define MMAP_USABLE             7   /* EfiConventionalMemory */
#define MMAP_ACPI_RECLAIMABLE   9
#define MMAP_ACPI_NVS          10
#define MMAP_RUNTIME_SERVICES  11   /* EfiRuntimeServicesCode/Data */
#define MMAP_BOOT_SERVICES     3    /* EfiBootServicesCode */
#define MMAP_BOOT_SERVICES_DATA 4

typedef struct {
    uint32_t type;
    uint32_t _pad;          /* UEFI pads after Type for alignment */
    uint64_t phys_start;
    uint64_t virt_start;
    uint64_t num_pages;
    uint64_t attributes;
} MemoryMapEntry;

typedef struct {
    uint64_t magic;

    /* Framebuffer (GOP) */
    uint64_t fb_base;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;       /* bytes per scanline */
    uint32_t fb_bpp;         /* bits per pixel (typically 32) */
    uint32_t fb_pixel_format; /* 0=BGRA, 1=RGBA */
    uint32_t _pad0;

    /* Memory map */
    uint64_t mmap_base;
    uint64_t mmap_size;
    uint64_t mmap_desc_size;
    uint32_t mmap_desc_version;
    uint32_t mmap_entry_count;

    /* Kernel location */
    uint64_t kernel_phys_base;
    uint64_t kernel_size;

    /* UEFI runtime */
    uint64_t runtime_services;
    uint64_t rsdp_address;
} PACKED BootInfo;

#endif /* VAULTOS_BOOT_INFO_H */
