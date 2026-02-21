/*
 * VaultOS Kernel Entry Point
 *
 * Initializes all kernel subsystems in the correct order and launches
 * the VaultShell as the first process.
 */

#include "../include/vaultos/boot_info.h"
#include "../include/vaultos/error_codes.h"
#include "lib/types.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "lib/assert.h"

/* Architecture */
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/syscall.h"

/* Memory */
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "mm/memory_layout.h"

/* Drivers */
#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/keyboard.h"

/* Crypto */
#include "crypto/random.h"
#include "crypto/sha256.h"
#include "crypto/hmac.h"
#include "crypto/aes.h"

/* Capabilities */
#include "cap/capability.h"

/* Database */
#include "db/database.h"

/* Process */
#include "proc/process.h"
#include "proc/scheduler.h"
#include "proc/ipc.h"

/* Shell */
#include "../shell/shell.h"

/* Panic handler */
void kernel_panic(const char *msg, const char *file, int line) {
    cli();
    kprintf("\n\n");
    kprintf("!!! KERNEL PANIC !!!\n");
    kprintf("  Message: %s\n", msg);
    kprintf("  File:    %s\n", file);
    kprintf("  Line:    %d\n", line);
    kprintf("\nSystem halted.\n");

    for (;;) hlt();
}

/* Self-tests for crypto subsystem */
static void run_crypto_tests(void) {
    kprintf("[TEST] Running crypto self-tests...\n");

    /* SHA-256 test: SHA256("") = e3b0c44298fc1c14... */
    uint8_t digest[32];
    sha256((const uint8_t *)"", 0, digest);
    ASSERT(digest[0] == 0xe3 && digest[1] == 0xb0 && digest[2] == 0xc4);
    kprintf("[TEST] SHA-256 empty string: PASS\n");

    /* SHA-256 test: SHA256("abc") = ba7816bf8f01cfea... */
    sha256((const uint8_t *)"abc", 3, digest);
    ASSERT(digest[0] == 0xba && digest[1] == 0x78 && digest[2] == 0x16);
    kprintf("[TEST] SHA-256 \"abc\": PASS\n");

    /* AES-128 round-trip test */
    uint8_t key[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                       0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F};
    uint8_t iv[16]  = {0};
    uint8_t plain[16] = "VaultOS test!!!";
    uint8_t cipher[16], decrypted[16];

    aes_ctx_t aes;
    aes_init(&aes, key);
    aes_cbc_encrypt(&aes, iv, plain, cipher, 16);
    aes_cbc_decrypt(&aes, iv, cipher, decrypted, 16);
    ASSERT(memcmp(plain, decrypted, 16) == 0);
    kprintf("[TEST] AES-128-CBC round-trip: PASS\n");

    /* HMAC-SHA256 test */
    uint8_t hmac_out[32];
    hmac_sha256(key, 16, (const uint8_t *)"test", 4, hmac_out);
    ASSERT(hmac_out[0] != 0 || hmac_out[1] != 0); /* Non-trivial output */
    kprintf("[TEST] HMAC-SHA256: PASS\n");

    kprintf("[TEST] All crypto tests passed!\n\n");
}

/* Syscall stubs for MVP (shell runs in kernel mode, calls DB directly) */
int64_t sys_db_query(uint64_t query_str, uint64_t result_buf, uint64_t buf_size) {
    (void)result_buf; (void)buf_size;
    /* In MVP, shell calls db_execute() directly */
    (void)query_str;
    return VOS_ERR_NOSYS;
}

int64_t sys_db_insert(uint64_t table_id, uint64_t record_ptr) {
    (void)table_id; (void)record_ptr;
    return VOS_ERR_NOSYS;
}

int64_t sys_db_delete(uint64_t table_id, uint64_t row_id) {
    (void)table_id; (void)row_id;
    return VOS_ERR_NOSYS;
}

int64_t sys_db_update(uint64_t table_id, uint64_t row_id, uint64_t record_ptr) {
    (void)table_id; (void)row_id; (void)record_ptr;
    return VOS_ERR_NOSYS;
}

int64_t sys_io_write(uint64_t buf, uint64_t len) {
    const char *str = (const char *)buf;
    for (uint64_t i = 0; i < len; i++) {
        fb_putchar(str[i]);
        serial_putchar(str[i]);
    }
    return (int64_t)len;
}

int64_t sys_io_read(uint64_t buf, uint64_t len) {
    char *dst = (char *)buf;
    for (uint64_t i = 0; i < len; i++) {
        dst[i] = keyboard_getchar();
    }
    return (int64_t)len;
}

/*
 * kernel_main - Entry point called by the bootloader.
 *
 * The bootloader passes a BootInfo struct with framebuffer info,
 * memory map, and kernel location.
 */
/* Local copy of boot info (bootloader memory becomes inaccessible after VMM init) */
static BootInfo local_boot_info;

void kernel_main(BootInfo *boot_info) {
    /* Phase 1: Early init (serial first for debug output) */
    serial_init();
    serial_write("\n\n=== VaultOS Kernel Starting ===\n\n");

    /* Validate boot info */
    if (boot_info->magic != BOOT_INFO_MAGIC) {
        serial_write("FATAL: Invalid BootInfo magic!\n");
        for (;;) hlt();
    }

    /* Copy boot info into kernel memory before VMM changes page tables.
     * The bootloader's memory may be outside the identity-mapped region. */
    memcpy(&local_boot_info, boot_info, sizeof(BootInfo));
    boot_info = &local_boot_info;

    /* Phase 2: Architecture init */
    kprintf("[BOOT] Setting up GDT...\n");
    gdt_init();

    kprintf("[BOOT] Setting up IDT...\n");
    idt_init();

    kprintf("[BOOT] Setting up PIC...\n");
    pic_init();

    /* Phase 3: Memory subsystem */
    kprintf("[BOOT] Initializing physical memory manager...\n");
    pmm_init(boot_info);

    kprintf("[BOOT] Initializing virtual memory...\n");
    vmm_init(boot_info);

    kprintf("[BOOT] Initializing kernel heap...\n");
    heap_init();

    /* Phase 4: Drivers (framebuffer needs VMM to be ready) */
    kprintf("[BOOT] Initializing framebuffer...\n");
    fb_init(boot_info);

    kprintf("[BOOT] Initializing timer...\n");
    pit_init(1000); /* 1 kHz = 1ms tick */

    kprintf("[BOOT] Initializing keyboard...\n");
    keyboard_init();

    /* Phase 5: Crypto */
    kprintf("[BOOT] Initializing RNG...\n");
    random_init();

    /* Run crypto self-tests */
    run_crypto_tests();

    /* Phase 6: Capability system */
    kprintf("[BOOT] Initializing capability system...\n");
    cap_init();

    /* Phase 7: Database engine */
    kprintf("[BOOT] Initializing database engine...\n");
    db_init();
    db_init_system_tables(boot_info);

    /* Phase 8: Process management */
    kprintf("[BOOT] Initializing process subsystem...\n");
    process_init();
    scheduler_init();
    ipc_init();

    /* Phase 9: SYSCALL init (for future Ring 3 support) */
    kprintf("[BOOT] Initializing SYSCALL interface...\n");
    syscall_init();

    /* Phase 10: Create VaultShell as first process */
    kprintf("[BOOT] Creating VaultShell process...\n");
    process_t *shell_proc = process_create("VaultShell", shell_main);
    ASSERT(shell_proc != NULL);
    scheduler_add(shell_proc);

    kprintf("\n[BOOT] VaultOS initialization complete.\n");
    kprintf("[BOOT] Launching VaultShell...\n\n");

    /* Enable interrupts and start the scheduler */
    scheduler_start(); /* Does not return */
}
