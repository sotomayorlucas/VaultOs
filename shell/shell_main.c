#include "shell.h"
#include "line_editor.h"
#include "display.h"
#include "../kernel/db/database.h"
#include "../kernel/drivers/framebuffer.h"
#include "../kernel/lib/printf.h"
#include "../kernel/lib/string.h"
#include "../kernel/mm/heap.h"
#include "../kernel/arch/x86_64/pit.h"
#include "../kernel/proc/process.h"

static void print_banner(void) {
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("\n");
    kprintf("  \\    /  /\\  | | |  |_ /\\  /__ \n");
    kprintf("   \\  /  /--\\ | | |  |  /--\\ __/ \n");
    kprintf("    \\/                            \n");
    kprintf("\n");
    fb_set_color(COLOR_CYAN, COLOR_VOS_BG);
    kprintf("  VaultOS v0.1.0 - Nanokernel Operating System\n");
    kprintf("  \"Everything is a database. All data is confidential.\"\n");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("\n  Type SHOW TABLES to see system tables.\n");
    kprintf("  Type DESCRIBE <table> to see table schema.\n");
    kprintf("  Type SELECT, INSERT, DELETE, UPDATE for data operations.\n");
    kprintf("  Type GRANT/REVOKE for capability management.\n\n");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
}

static void print_prompt(void) {
    fb_set_color(COLOR_VOS_HL, COLOR_VOS_BG);
    kprintf("vaultos");
    fb_set_color(COLOR_GRAY, COLOR_VOS_BG);
    kprintf("> ");
    fb_set_color(COLOR_VOS_FG, COLOR_VOS_BG);
}

void shell_main(void) {
    char line_buf[LINE_MAX];

    print_banner();

    while (true) {
        print_prompt();
        char *line = line_read(line_buf, sizeof(line_buf));

        if (!line || strlen(line) == 0) continue;

        /* Built-in commands */
        if (strcasecmp(line, "clear") == 0) {
            fb_clear();
            continue;
        }
        if (strcasecmp(line, "help") == 0) {
            kprintf("\nVaultOS Query Commands:\n");
            kprintf("  SHOW TABLES                          - List all tables\n");
            kprintf("  DESCRIBE <table>                     - Show table schema\n");
            kprintf("  SELECT * FROM <table> [WHERE ...]    - Query data\n");
            kprintf("  INSERT INTO <table> (cols) VALUES ... - Insert data\n");
            kprintf("  DELETE FROM <table> [WHERE ...]      - Delete data\n");
            kprintf("  UPDATE <table> SET col=val [WHERE ...] - Update data\n");
            kprintf("  GRANT <rights> ON <obj> TO <pid>     - Grant capability\n");
            kprintf("  REVOKE <cap_id>                      - Revoke capability\n");
            kprintf("\nBuilt-in:\n");
            kprintf("  help   - This help message\n");
            kprintf("  clear  - Clear screen\n");
            kprintf("  status - System status\n\n");
            continue;
        }
        if (strcasecmp(line, "status") == 0) {
            kprintf("\n--- VaultOS System Status ---\n");
            kprintf("  Uptime: %llu ms\n", pit_get_uptime_ms());
            kprintf("  Heap used: %llu bytes\n", (uint64_t)heap_used());
            kprintf("  Heap free: %llu bytes\n", (uint64_t)heap_free());
            kprintf("  Tables: %u\n", db_get_table_count());
            kprintf("-----------------------------\n\n");
            continue;
        }

        /* Get current PID (kernel PID = 0 for MVP) */
        uint64_t pid = 0;
        process_t *cur = process_get_current();
        if (cur) pid = cur->pid;

        /* Execute query */
        query_result_t *result = db_execute(line, pid);
        display_result(result);
        db_result_free(result);
    }
}
